// ============================================================================
// core/hud_manager.h
// Manages all HUD display elements and coordinates their rendering and updates
// ============================================================================
#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include "../vendor/piboso/mxb_api.h"
#include "../hud/base_hud.h"

// Forward declarations to avoid circular dependency with plugin_data.h
enum class DataChangeType;

class HudManager {
public:
    static HudManager& getInstance();

    void initialize();
    void shutdown();
    void clear();

    // Resource management - called from plugin manager
    int initializeResources(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName);

    // Called from plugin manager during draw operations
    void draw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString);

    // HUD registration
    void registerHud(std::unique_ptr<BaseHud> hud);

    // Data change notification callback (called by PluginData)
    void onDataChanged(DataChangeType changeType);

    // Validate all HUD positions fit within current window bounds
    // Call this after resolution changes
    void validateAllHudPositions();

    // Mark all HUDs as needing rebuild (e.g., after color config change)
    void markAllHudsDirty();

    // Check if HudManager is initialized
    bool isInitialized() const { return m_bInitialized; }

    // Check if settings menu is currently visible
    bool isSettingsVisible() const;

    // Widgets master toggle (hides all widgets without changing individual states)
    bool areWidgetsEnabled() const { return !m_bAllWidgetsToggledOff; }
    void setWidgetsEnabled(bool enabled) { m_bAllWidgetsToggledOff = !enabled; }

    // Track centerline data handling
    void updateTrackCenterline(int numSegments, SPluginsTrackSegment_t* segments);

    // Rider position data handling (high-frequency update)
    void updateRiderPositions(int numVehicles, SPluginsRaceTrackPosition_t* positions);

    // Radar HUD position update (called alongside MapHud)
    void updateRadarPositions(int numVehicles, SPluginsRaceTrackPosition_t* positions);

    // Get HUD references for settings persistence
    // Note: These assert m_bInitialized - only call after initialize() and before shutdown()
    class IdealLapHud& getIdealLapHud() const { assert(m_pIdealLap && "HudManager not initialized"); return *m_pIdealLap; }
    class LapLogHud& getLapLogHud() const { assert(m_pLapLog && "HudManager not initialized"); return *m_pLapLog; }
    class StandingsHud& getStandingsHud() const { assert(m_pStandings && "HudManager not initialized"); return *m_pStandings; }
    class PerformanceHud& getPerformanceHud() const { assert(m_pPerformance && "HudManager not initialized"); return *m_pPerformance; }
    class TelemetryHud& getTelemetryHud() const { assert(m_pTelemetry && "HudManager not initialized"); return *m_pTelemetry; }
    class TimeWidget& getTimeWidget() const { assert(m_pTime && "HudManager not initialized"); return *m_pTime; }
    class PositionWidget& getPositionWidget() const { assert(m_pPosition && "HudManager not initialized"); return *m_pPosition; }
    class LapWidget& getLapWidget() const { assert(m_pLap && "HudManager not initialized"); return *m_pLap; }
    class SessionWidget& getSessionWidget() const { assert(m_pSession && "HudManager not initialized"); return *m_pSession; }
    class MapHud& getMapHud() const { assert(m_pMapHud && "HudManager not initialized"); return *m_pMapHud; }
    class RadarHud& getRadarHud() const { assert(m_pRadarHud && "HudManager not initialized"); return *m_pRadarHud; }
    class SpeedWidget& getSpeedWidget() const { assert(m_pSpeed && "HudManager not initialized"); return *m_pSpeed; }
    class SpeedoWidget& getSpeedoWidget() const { assert(m_pSpeedo && "HudManager not initialized"); return *m_pSpeedo; }
    class TachoWidget& getTachoWidget() const { assert(m_pTacho && "HudManager not initialized"); return *m_pTacho; }
    class TimingHud& getTimingHud() const { assert(m_pTiming && "HudManager not initialized"); return *m_pTiming; }
    class BarsWidget& getBarsWidget() const { assert(m_pBars && "HudManager not initialized"); return *m_pBars; }
    class VersionWidget& getVersionWidget() const { assert(m_pVersion && "HudManager not initialized"); return *m_pVersion; }
    class NoticesWidget& getNoticesWidget() const { assert(m_pNotices && "HudManager not initialized"); return *m_pNotices; }
    class SettingsButtonWidget& getSettingsButtonWidget() const { assert(m_pSettingsButton && "HudManager not initialized"); return *m_pSettingsButton; }
    class PitboardHud& getPitboardHud() const { assert(m_pPitboard && "HudManager not initialized"); return *m_pPitboard; }
    class RecordsHud& getRecordsHud() const { assert(m_pRecords && "HudManager not initialized"); return *m_pRecords; }
    class FuelWidget& getFuelWidget() const { assert(m_pFuel && "HudManager not initialized"); return *m_pFuel; }
    class GapBarHud& getGapBarHud() const { assert(m_pGapBar && "HudManager not initialized"); return *m_pGapBar; }
    class PointerWidget& getPointerWidget() const { assert(m_pPointer && "HudManager not initialized"); return *m_pPointer; }
    class RumbleHud& getRumbleHud() const { assert(m_pRumble && "HudManager not initialized"); return *m_pRumble; }
    class GamepadWidget& getGamepadWidget() const { assert(m_pGamepad && "HudManager not initialized"); return *m_pGamepad; }
    class LeanWidget& getLeanWidget() const { assert(m_pLean && "HudManager not initialized"); return *m_pLean; }
    class SettingsHud& getSettingsHud() const { assert(m_pSettingsHud && "HudManager not initialized"); return *m_pSettingsHud; }

private:
    HudManager() : m_bInitialized(false), m_bResourcesInitialized(false),
                   m_pDraggingHud(nullptr), m_pSettingsHud(nullptr), m_pSettingsButton(nullptr),
                   m_pIdealLap(nullptr), m_pLapLog(nullptr), m_pStandings(nullptr),
                   m_pPerformance(nullptr), m_pTelemetry(nullptr),
                   m_pTime(nullptr), m_pPosition(nullptr), m_pLap(nullptr), m_pSession(nullptr), m_pMapHud(nullptr), m_pRadarHud(nullptr), m_pSpeed(nullptr), m_pSpeedo(nullptr), m_pTacho(nullptr), m_pTiming(nullptr), m_pGapBar(nullptr), m_pBars(nullptr), m_pVersion(nullptr), m_pNotices(nullptr), m_pPitboard(nullptr), m_pRecords(nullptr), m_pFuel(nullptr), m_pPointer(nullptr), m_pRumble(nullptr), m_pGamepad(nullptr), m_pLean(nullptr),
                   m_bAllHudsToggledOff(false), m_bAllWidgetsToggledOff(false) {
    }
    ~HudManager();
    HudManager(const HudManager&) = delete;
    HudManager& operator=(const HudManager&) = delete;

    void updateHuds();
    void processKeyboardInput();
    void collectRenderData();
    void setupDefaultResources();
    void handleSettingsButton();

    bool m_bInitialized;
    bool m_bResourcesInitialized;
    std::vector<std::unique_ptr<BaseHud>> m_huds;

    // Cache pointer to currently dragging HUD (eliminates search loop every frame)
    BaseHud* m_pDraggingHud;

    // Pointers to HUDs (for settings persistence and button clicks)
    class SettingsHud* m_pSettingsHud;
    class SettingsButtonWidget* m_pSettingsButton;
    class IdealLapHud* m_pIdealLap;
    class LapLogHud* m_pLapLog;
    class StandingsHud* m_pStandings;
    class PerformanceHud* m_pPerformance;
    class TelemetryHud* m_pTelemetry;
    class TimeWidget* m_pTime;
    class PositionWidget* m_pPosition;
    class LapWidget* m_pLap;
    class SessionWidget* m_pSession;
    class MapHud* m_pMapHud;
    class RadarHud* m_pRadarHud;
    class SpeedWidget* m_pSpeed;
    class SpeedoWidget* m_pSpeedo;
    class TachoWidget* m_pTacho;
    class TimingHud* m_pTiming;
    class GapBarHud* m_pGapBar;
    class BarsWidget* m_pBars;
    class VersionWidget* m_pVersion;
    class NoticesWidget* m_pNotices;
    class PitboardHud* m_pPitboard;
    class RecordsHud* m_pRecords;
    class FuelWidget* m_pFuel;
    class PointerWidget* m_pPointer;
    class RumbleHud* m_pRumble;
    class GamepadWidget* m_pGamepad;
    class LeanWidget* m_pLean;

    // Temporary HUD visibility toggle (doesn't modify actual visibility state)
    bool m_bAllHudsToggledOff;
    bool m_bAllWidgetsToggledOff;

    // Collected render data from all HUDs
    std::vector<SPluginQuad_t> m_quads;
    std::vector<SPluginString_t> m_strings;

    // Resource management - dynamically sized based on discovered assets
    std::vector<std::string> m_spriteNames;
    std::vector<std::string> m_fontNames;

    std::vector<char> m_spriteBuffer;  // Null-separated sprite names for API
    std::vector<char> m_fontBuffer;    // Null-separated font names for API

    // Performance optimization constants
    // Set to typical first-frame usage to avoid multiple reallocations
    // PerformanceHud: ~250 quads, MapHud: ~700 quads, StandingsHud: 2 quads + ~90 strings
    static constexpr size_t INITIAL_QUAD_CAPACITY = 2000;  // Accounts for map + performance graphs
    static constexpr size_t INITIAL_STRING_CAPACITY = 400;  // Accounts for standings + labels
    static constexpr size_t CAPACITY_GROWTH_FACTOR = 2;
};
