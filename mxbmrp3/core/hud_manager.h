// ============================================================================
// core/hud_manager.h
// Manages all HUD display elements and coordinates their rendering and updates
// ============================================================================
#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <chrono>
#include <functional>
#include "../game/game_config.h"
#include "../game/unified_types.h"
#include "../hud/base_hud.h"

// Forward declarations to avoid circular dependency with plugin_data.h
enum class DataChangeType;

class HudManager {
public:
    static HudManager& getInstance();

    void initialize();
    void shutdown();
    // allowCrossSingleton=false on the static-teardown (destructor) path, where the
    // CompanionWindow singleton this would reach may already be destroyed. See the .cpp.
    void clear(bool allowCrossSingleton = true);

    // Resource management - called from plugin manager
    int initializeResources(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName);

    // Called from plugin manager during draw operations (synchronous / game-thread mode)
    //
    // ALL QUADS DRAW BEFORE ALL STRINGS, ACROSS EVERY HUD, and this is not ours to
    // change: the plugin API hands the game two SEPARATE arrays (`ppQuad`, then
    // `ppString`), so it renders them as two batches. Consequences worth knowing
    // before you go looking for a bug that is not there:
    //   - One HUD's text can appear over ANOTHER HUD's background. Registration
    //     order and per-HUD z-order do not help; they only order within a batch.
    //   - A HUD cannot occlude text with an opaque panel. Overlapping HUDs will
    //     show each other's strings through the top one, and the only real fixes
    //     are to move a HUD or hide it.
    // Reproducible in a screenshot: the settings panel shows other HUDs' rows
    // through it (companion_demo.sh, any `tab` scene, with HUDs behind it).
    void draw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString);

    // Run the full per-frame update + collect for the given draw state, leaving the
    // built in-game frame in m_quads/m_strings and the companion frame submitted. This
    // is the shared body of draw(); the experimental plugin worker thread calls it
    // directly (off the game thread) and then reads gameFrameQuads()/inGameFrameSuppressed()
    // to publish a triple-buffered copy. See core/plugin_thread.{h,cpp}.
    void produceFrame(int iState);
    // After produceFrame(): the in-game frame, and whether the display target
    // (COMPANION mode) means the game should be handed an empty frame this pass.
    const std::vector<SPluginQuad_t>& gameFrameQuads() const { return m_quads; }
    const std::vector<SPluginString_t>& gameFrameStrings() const { return m_strings; }
    bool inGameFrameSuppressed() const { return m_bSuppressInGame; }

    // Read-only access to the last-collected surface frames, for test introspection
    // of the game vs companion render routing (see collectSurface / core/test_hooks.cpp).
    const std::vector<SPluginQuad_t>& getGameQuads() const { return m_quads; }
    const std::vector<SPluginQuad_t>& getCompanionQuads() const { return m_companionQuads; }

    // Every registered HUD, in draw order. For test introspection only (the grid
    // sweep in core/test_hooks.cpp): asking "is ANY panel off the lattice" needs the
    // whole set, and a per-HUD id on the panel-rect hook does not scale to that.
    const std::vector<std::unique_ptr<BaseHud>>& getHuds() const { return m_huds; }

    // HUD registration. `harnessId` is REQUIRED and stamps BaseHud::getHarnessId
    // -- the one stable name for this element, read by the benchmark report and
    // the test harness alike (see setHarnessId for the two ladders it replaced).
    // Required rather than defaulted precisely because the failure it prevents is
    // an element registered without a name.
    void registerHud(std::unique_ptr<BaseHud> hud, const char* harnessId);

    // Panels that SHOULD carry a benchmark slot: every registered HUD except the
    // BenchmarkWidget, which excludes itself from its own profile. Exposed for the
    // test that pins "no panel is silently left unprofiled" (see MAX_HUDS).
    // Defined in the .cpp: BenchmarkWidget is only forward-declared here, so the
    // derived-to-base comparison needs the complete type.
    int profilableHudCount() const;

    // The sprite table as DrawInit registered it, for the benchmark report's render
    // probe block. 1-BASED to match SPluginQuad_t::m_iSprite (0 there means "no
    // sprite, fill with the colour"), so a reported index can be pasted straight
    // into renderProbeSprite. Empty string for an out-of-range index.
    int registeredSpriteCount() const { return static_cast<int>(m_spriteNames.size()); }
    const char* spriteName(int oneBasedIndex) const {
        return (oneBasedIndex >= 1 && oneBasedIndex <= registeredSpriteCount())
            ? m_spriteNames[static_cast<size_t>(oneBasedIndex - 1)].c_str() : "";
    }

    // Result of verifySpriteRegistrationOrder(), run at the end of
    // setupDefaultResources(): 0 = every index discovery recorded points at its
    // own file in the registered sprite table. Non-zero means the discovery /
    // registration orders diverged and some asset is drawing another's sprites
    // (logged per mismatch). Pinned by sprite_order_test.cpp.
    int spriteOrderMismatches() const { return m_spriteOrderMismatches; }

#ifdef MXBMRP3_TEST_BUILD
    // Test-only must-catch seam: re-run the order check with table entries a and
    // b (1-based) swapped, then restore. A checker that returns 0 here too would
    // be vacuous -- sprite_order_test.cpp asserts this returns non-zero.
    int testSpriteOrderWithSwap(int aOneBased, int bOneBased);
#endif

    // Bind a typed HUD cache pointer (m_pStandings, ...) to the slot-clearing
    // list: clear() nulls every bound pointer before destroying the HUD
    // objects. Binding happens at registration, so a cached pointer that could
    // dangle after clear() cannot exist by construction — the hand-maintained
    // null list this replaces was a known dangling-pointer trap (same
    // declaration-is-registration idea as PerRider<>).
    template <typename T>
    void bindHudSlot(T*& cachePtr) {
        m_hudSlotClearers.push_back([&cachePtr] { cachePtr = nullptr; });
    }

    // Construct a HUD, bind its typed cache pointer (see bindHudSlot), and
    // register it in draw order under `name`. The standard one-liner for
    // initialize(); HUDs needing constructor args or deferred registration
    // (SettingsHud, SettingsButtonWidget, PointerWidget) compose the pieces by
    // hand and call registerHud themselves.
    //
    // `name` is the HARNESS ID ONLY -- deliberately NOT also the texture base name,
    // though for every element that has one the two strings are equal. A HUD's
    // texture stem is declared once, by the HUD, in its constructor, because the
    // constructor CONSUMES it: resetToDefaults() runs there and calls
    // setTextureVariant(), which resolves the sprite through
    // AssetManager::getSpriteIndex(m_textureBaseName, variant). Setting the stem
    // out here would land after that, and a HUD whose artwork is mandatory
    // (m_textureRequired -- Radar, Speedo, Tacho, Pitboard, Gamepad) would resolve
    // its dial against an empty name. Restating it here was a duplicate declaration
    // with nothing checking the two agreed, and one already had not.
    template <typename T>
    T* createHud(T*& cachePtr, const char* name) {
        auto hud = std::make_unique<T>();
        cachePtr = hud.get();
        bindHudSlot(cachePtr);
        registerHud(std::move(hud), name);
        return cachePtr;
    }

    // Data change notification callback (called by PluginData)
    void onDataChanged(DataChangeType changeType);

    // Validate all HUD positions fit within current window bounds
    // Call this after resolution changes
    void validateAllHudPositions();
    // Ask for the above to run at the END of the next updateHuds() pass, instead of
    // right now. Call this from anything reachable from a HUD's update() -- an input
    // handler, most of all. validatePosition() calls update() on a dirty HUD, so
    // validating from inside a handler re-enters the very update that dispatched it:
    // the click edge is still true for the rest of the frame, so it dispatches again,
    // and again. That recursion shipped and cost two stack-overflow crashes
    // (0xC00000FD, ~1400 frames deep); see teardown of the theme-cycle handler.
    void requestPositionValidation() { m_validationPending = true; }

    // Mark all HUDs as needing rebuild (e.g., after color config change)
    void markAllHudsDirty();

    // Rebuild all dirty HUDs immediately (without full update logic)
    // Use after batch settings changes to ensure quads are updated before render
    void rebuildAllIfDirty();

    // Check if HudManager is initialized
    bool isInitialized() const { return m_bInitialized; }

    // Check if settings menu is currently visible
    bool isSettingsVisible() const;

    // Visibility queries for optimization (allows PluginData to skip processing)
    // Returns true if TelemetryHud is visible and needs history buffer updates
    bool isTelemetryHistoryNeeded() const;

    // Widgets master toggle (hides all widgets without changing individual states)
    bool areWidgetsEnabled() const { return !m_bAllWidgetsToggledOff; }
    void setWidgetsEnabled(bool enabled) { m_bAllWidgetsToggledOff = !enabled; }

    // Track centerline data handling
    // raceData: float array [S/F, split1, split2, holeshot] in meters along centerline,
    // or nullptr if unavailable. Values <= 0 are treated as "not present".
    void updateTrackCenterline(int numSegments, Unified::TrackSegment* segments, const float* raceData);

    // Rider position data handling (high-frequency update)
    void updateRiderPositions(int numVehicles, Unified::TrackPositionData* positions);

    // Get HUD references for settings persistence
    // Note: These assert m_bInitialized - only call after initialize() and before shutdown()
    class IdealLapHud& getIdealLapHud() const { assert(m_pIdealLap && "HudManager not initialized"); return *m_pIdealLap; }
    class LapLogHud& getLapLogHud() const { assert(m_pLapLog && "HudManager not initialized"); return *m_pLapLog; }
    class FriendsHud& getFriendsHud() const { assert(m_pFriends && "HudManager not initialized"); return *m_pFriends; }
    class StandingsHud& getStandingsHud() const { assert(m_pStandings && "HudManager not initialized"); return *m_pStandings; }
    class PerformanceHud& getPerformanceHud() const { assert(m_pPerformance && "HudManager not initialized"); return *m_pPerformance; }
    class TelemetryHud& getTelemetryHud() const { assert(m_pTelemetry && "HudManager not initialized"); return *m_pTelemetry; }
    class TimeWidget& getTimeWidget() const { assert(m_pTime && "HudManager not initialized"); return *m_pTime; }
    class SpotterWidget& getSpotterWidget() const { assert(m_pSpotter && "HudManager not initialized"); return *m_pSpotter; }
    class PositionWidget& getPositionWidget() const { assert(m_pPosition && "HudManager not initialized"); return *m_pPosition; }
    class LapWidget& getLapWidget() const { assert(m_pLap && "HudManager not initialized"); return *m_pLap; }
    class SessionHud& getSessionHud() const { assert(m_pSession && "HudManager not initialized"); return *m_pSession; }
    class MapHud& getMapHud() const { assert(m_pMapHud && "HudManager not initialized"); return *m_pMapHud; }
    class RadarHud& getRadarHud() const { assert(m_pRadarHud && "HudManager not initialized"); return *m_pRadarHud; }
    class SpeedWidget& getSpeedWidget() const { assert(m_pSpeed && "HudManager not initialized"); return *m_pSpeed; }
    class GearWidget& getGearWidget() const { assert(m_pGear && "HudManager not initialized"); return *m_pGear; }
    class CrashWidget& getCrashWidget() const { assert(m_pCrash && "HudManager not initialized"); return *m_pCrash; }
    class SpeedoWidget& getSpeedoWidget() const { assert(m_pSpeedo && "HudManager not initialized"); return *m_pSpeedo; }
    class TachoWidget& getTachoWidget() const { assert(m_pTacho && "HudManager not initialized"); return *m_pTacho; }
    class TimingHud& getTimingHud() const { assert(m_pTiming && "HudManager not initialized"); return *m_pTiming; }
    class BarsWidget& getBarsWidget() const { assert(m_pBars && "HudManager not initialized"); return *m_pBars; }
    class VersionWidget& getVersionWidget() const { assert(m_pVersion && "HudManager not initialized"); return *m_pVersion; }
    class NoticesHud& getNoticesHud() const { assert(m_pNotices && "HudManager not initialized"); return *m_pNotices; }
    class SettingsButtonWidget& getSettingsButtonWidget() const { assert(m_pSettingsButton && "HudManager not initialized"); return *m_pSettingsButton; }
    class PitboardHud& getPitboardHud() const { assert(m_pPitboard && "HudManager not initialized"); return *m_pPitboard; }
#if GAME_HAS_RECORDS_PROVIDER
    class RecordsHud& getRecordsHud() const { assert(m_pRecords && "HudManager not initialized"); return *m_pRecords; }
#endif
    class FuelWidget& getFuelWidget() const { assert(m_pFuel && "HudManager not initialized"); return *m_pFuel; }
    class GapBarHud& getGapBarHud() const { assert(m_pGapBar && "HudManager not initialized"); return *m_pGapBar; }
    class PointerWidget& getPointerWidget() const { assert(m_pPointer && "HudManager not initialized"); return *m_pPointer; }
    class RumbleHud& getRumbleHud() const { assert(m_pRumble && "HudManager not initialized"); return *m_pRumble; }
    class DirectorWidget* getDirectorWidget() const { return m_pDirector; }  // nullable; callers null-check
    class GamepadWidget& getGamepadWidget() const { assert(m_pGamepad && "HudManager not initialized"); return *m_pGamepad; }
    class LeanWidget& getLeanWidget() const { assert(m_pLean && "HudManager not initialized"); return *m_pLean; }
    class GForceWidget& getGForceWidget() const { assert(m_pGforce && "HudManager not initialized"); return *m_pGforce; }
    class CompassWidget& getCompassWidget() const { assert(m_pCompass && "HudManager not initialized"); return *m_pCompass; }
    class ClockWidget& getClockWidget() const { assert(m_pClock && "HudManager not initialized"); return *m_pClock; }
#if GAME_HAS_TYRE_TEMP
    class TyreTempWidget& getTyreTempWidget() const { assert(m_pTyreTemp && "HudManager not initialized"); return *m_pTyreTemp; }
#endif
#if GAME_HAS_ECU
    class EcuWidget& getEcuWidget() const { assert(m_pEcu && "HudManager not initialized"); return *m_pEcu; }
#endif
    class SessionChartsHud& getSessionChartsHud() const { assert(m_pSessionCharts && "HudManager not initialized"); return *m_pSessionCharts; }
    class HelmetOverlayHud& getHelmetOverlayHud() const { assert(m_pHelmetOverlay && "HudManager not initialized"); return *m_pHelmetOverlay; }
#if GAME_HAS_FMX
    class FmxHud& getFmxHud() const { assert(m_pFmxHud && "HudManager not initialized"); return *m_pFmxHud; }
#endif
    class StatsHud& getStatsHud() const { assert(m_pStatsHud && "HudManager not initialized"); return *m_pStatsHud; }
    class EventLogHud& getEventLogHud() const { assert(m_pEventLog && "HudManager not initialized"); return *m_pEventLog; }
    class BenchmarkWidget* getBenchmarkWidget() const { return m_pBenchmark; }  // May be null if developer mode is off
    class SettingsHud& getSettingsHud() const { assert(m_pSettingsHud && "HudManager not initialized"); return *m_pSettingsHud; }

#if defined(MXBMRP3_TEST_BUILD)
    // Force every registered HUD/widget visible (or hidden), skipping UI chrome
    // (settings menu, settings button, pointer, benchmark). Used by the headless
    // benchmark driver to profile the plugin with everything enabled; not part of
    // any in-game flow, so it's compiled out of every shipping DLL.
    void testSetAllHudsVisible(bool visible);

    // Sprite span of the frame last handed to the game: the lowest and highest
    // non-zero m_iSprite, and how many quads used sprite 0 (the UNTEXTURED path,
    // "fill with m_ulColor"). Both spans are 0 when the frame has no textured quad.
    //
    // Exists for the render probe's test, and specifically for its one silent trap:
    // an out-of-range renderProbeSprite must fall back to CYCLING, never to sprite
    // 0, because sprite 0 would quietly measure flat fill while the report claimed
    // to be measuring the textured path -- a wrong answer that looks like a result.
    // The harness cannot check this itself; it holds no mirror of SPluginQuad_t and
    // deliberately treats the API payloads as opaque.
    void testLastFrameSpriteSpan(int& minSprite, int& maxSprite, int& untexturedCount) const;

    // Install a synthetic sprite table of `count` entries, for the render probe's
    // test. The real table is built in DrawInit from files on disk, and the headless
    // harness stages no assets -- so the probe's type-1 branch, which is index
    // arithmetic over m_spriteNames.size(), would otherwise be untestable: with an
    // empty table it legitimately degenerates to the untextured path, and every
    // assertion about pinning would pass vacuously.
    void testInstallSpriteTable(int count);

    // Inject an artificial per-frame stall into produceFrame() (ms), standing in for a
    // slow component like the Map HUD's ribbon tessellation. Used to demonstrate that
    // such a stall blocks the game's Draw in sync mode but not in plugin-thread mode.
    // 0 disables. Compiled out of every shipping DLL.
    static void testSetProduceDelayMs(int ms);
#endif

private:
    HudManager() : m_bInitialized(false), m_bResourcesInitialized(false),
                   m_pDraggingHud(nullptr), m_pSettingsHud(nullptr), m_pSettingsButton(nullptr),
                   m_pIdealLap(nullptr), m_pLapLog(nullptr), m_pFriends(nullptr), m_pStandings(nullptr),
                   m_pPerformance(nullptr), m_pTelemetry(nullptr),
                   m_pTime(nullptr), m_pSpotter(nullptr), m_pPosition(nullptr), m_pLap(nullptr), m_pSession(nullptr), m_pMapHud(nullptr), m_pRadarHud(nullptr), m_pSpeed(nullptr), m_pGear(nullptr), m_pCrash(nullptr), m_pSpeedo(nullptr), m_pTacho(nullptr), m_pTiming(nullptr), m_pGapBar(nullptr), m_pBars(nullptr), m_pVersion(nullptr), m_pNotices(nullptr), m_pPitboard(nullptr),
#if GAME_HAS_RECORDS_PROVIDER
                   m_pRecords(nullptr),
#endif
                   m_pFuel(nullptr), m_pPointer(nullptr), m_pRumble(nullptr), m_pDirector(nullptr), m_pGamepad(nullptr), m_pLean(nullptr), m_pGforce(nullptr), m_pCompass(nullptr), m_pClock(nullptr),
#if GAME_HAS_TYRE_TEMP
                   m_pTyreTemp(nullptr),
#endif
#if GAME_HAS_ECU
                   m_pEcu(nullptr),
#endif
                   m_pSessionCharts(nullptr),
                   m_pHelmetOverlay(nullptr),
                   m_pStatsHud(nullptr),
                   m_pFmxHud(nullptr),
                   m_pEventLog(nullptr),
                   m_pBenchmark(nullptr),
                   m_bAllHudsToggledOff(false), m_bAllWidgetsToggledOff(false) {
    }
    ~HudManager();
    HudManager(const HudManager&) = delete;
    HudManager& operator=(const HudManager&) = delete;

    // Shared teardown. allowSave gates the cross-singleton settings auto-save:
    // true on the orchestrated Shutdown() path (every singleton still alive),
    // false from ~HudManager() during static teardown (SettingsManager may be
    // gone). See the definitions in hud_manager.cpp.
    void shutdownInternal(bool allowSave);

    void updateHuds();
    void processKeyboardInput();
    void collectRenderData();
    // Build one surface's frame into the given vectors. companion=false reproduces
    // the game frame exactly; companion=true uses each HUD's companion instance
    // (on/off + position). See collectRenderData.
    void collectSurface(std::vector<SPluginQuad_t>& outQuads,
                        std::vector<SPluginString_t>& outStrings, bool companion);
    // Debug/alignment aid (INI-only, off by default): append the HUD snap-grid lattice
    // as thin quads on top of the frame. Every Nth line uses the "major" color/thickness.
    void appendGridOverlay(std::vector<SPluginQuad_t>& outQuads) const;
    // Number of grid quads appendGridOverlay would add (for capacity reservation).
    static size_t gridOverlayQuadCount();
    void setupDefaultResources();
    // Cross-check every sprite index recorded at discovery against the table
    // setupDefaultResources() just registered; returns the mismatch count.
    // Startup-only cost. See the definition for the invariant it verifies.
    int verifySpriteRegistrationOrder(const class AssetManager& assetMgr) const;
    void handleSettingsButton();
    void handleDirectorButton();
    void persistDirectorEnabled();  // save the director on/off mode (auto-save-gated)

    bool m_bInitialized;

    // Deferred validateAllHudPositions(); see requestPositionValidation().

    bool m_validationPending = false;
    bool m_bResourcesInitialized;
    std::vector<std::unique_ptr<BaseHud>> m_huds;
    // One entry per bound cache pointer (bindHudSlot); run + emptied by clear().
    std::vector<std::function<void()>> m_hudSlotClearers;

    // Cache pointer to currently dragging HUD (eliminates search loop every frame)
    BaseHud* m_pDraggingHud;

    // Timestamp of the previous Draw call. A long gap means the game stopped drawing us
    // (menu / loading); when it resumes we treat it as "entered the track" and flash the
    // corner status buttons into view. Default-constructed to the clock epoch.
    std::chrono::steady_clock::time_point m_lastDrawTime;

    // Pointers to HUDs (for settings persistence and button clicks)
    class SettingsHud* m_pSettingsHud;
    class SettingsButtonWidget* m_pSettingsButton;
    class IdealLapHud* m_pIdealLap;
    class LapLogHud* m_pLapLog;
    class FriendsHud* m_pFriends;
    class StandingsHud* m_pStandings;
    class PerformanceHud* m_pPerformance;
    class TelemetryHud* m_pTelemetry;
    class TimeWidget* m_pTime;
    class SpotterWidget* m_pSpotter;
    class PositionWidget* m_pPosition;
    class LapWidget* m_pLap;
    class SessionHud* m_pSession;
    class MapHud* m_pMapHud;
    class RadarHud* m_pRadarHud;
    class SpeedWidget* m_pSpeed;
    class GearWidget* m_pGear;
    class CrashWidget* m_pCrash;
    class SpeedoWidget* m_pSpeedo;
    class TachoWidget* m_pTacho;
    class TimingHud* m_pTiming;
    class GapBarHud* m_pGapBar;
    class BarsWidget* m_pBars;
    class VersionWidget* m_pVersion;
    class NoticesHud* m_pNotices;
    class PitboardHud* m_pPitboard;
#if GAME_HAS_RECORDS_PROVIDER
    class RecordsHud* m_pRecords;
#endif
    class FuelWidget* m_pFuel;
    class PointerWidget* m_pPointer;
    class RumbleHud* m_pRumble;
    class DirectorWidget* m_pDirector;
    class GamepadWidget* m_pGamepad;
    class LeanWidget* m_pLean;
    class GForceWidget* m_pGforce;
    class CompassWidget* m_pCompass;
    class ClockWidget* m_pClock;
#if GAME_HAS_TYRE_TEMP
    class TyreTempWidget* m_pTyreTemp;
#endif
#if GAME_HAS_ECU
    class EcuWidget* m_pEcu;
#endif
    class SessionChartsHud* m_pSessionCharts;
    class HelmetOverlayHud* m_pHelmetOverlay;
    class StatsHud* m_pStatsHud;
    class FmxHud* m_pFmxHud;
    class EventLogHud* m_pEventLog;
    class BenchmarkWidget* m_pBenchmark;

    // Temporary HUD visibility toggle (doesn't modify actual visibility state)
    bool m_bAllHudsToggledOff;
    bool m_bAllWidgetsToggledOff;
    // Set by produceFrame(): true when the display target (COMPANION) means the game
    // gets an empty frame this pass. Read by draw() and by the plugin worker thread.
    bool m_bSuppressInGame = false;
    bool m_lastActiveCompanion = false;  // track focus surface to refresh settings on change

    // Collected render data from all HUDs
    std::vector<SPluginQuad_t> m_quads;
    std::vector<SPluginString_t> m_strings;
    // Companion-surface frame (built only while the companion window is open).
    std::vector<SPluginQuad_t> m_companionQuads;
    std::vector<SPluginString_t> m_companionStrings;

    // Resource management - dynamically sized based on discovered assets
    std::vector<std::string> m_spriteNames;
    std::vector<std::string> m_fontNames;
    // Written once by setupDefaultResources() on the game thread; see the getter.
    int m_spriteOrderMismatches = 0;

    std::vector<char> m_spriteBuffer;  // Null-separated sprite names for API
    std::vector<char> m_fontBuffer;    // Null-separated font names for API

    // Performance optimization constants
    // Set to typical first-frame usage to avoid multiple reallocations
    // PerformanceHud: ~250 quads, MapHud: ~700 quads, StandingsHud: 2 quads + ~90 strings
    static constexpr size_t INITIAL_QUAD_CAPACITY = 2000;  // Accounts for map + performance graphs
    static constexpr size_t INITIAL_STRING_CAPACITY = 400;  // Accounts for standings + labels
    static constexpr size_t CAPACITY_GROWTH_FACTOR = 2;
};
