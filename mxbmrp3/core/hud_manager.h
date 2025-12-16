// ============================================================================
// core/hud_manager.h
// Manages all HUD display elements and coordinates their rendering and updates
// ============================================================================
#pragma once

#include <vector>
#include <memory>
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
    class SessionBestHud& getSessionBestHud() const { return *m_pSessionBest; }
    class LapLogHud& getLapLogHud() const { return *m_pLapLog; }
    class StandingsHud& getStandingsHud() const { return *m_pStandings; }
    class PerformanceHud& getPerformanceHud() const { return *m_pPerformance; }
    class TelemetryHud& getTelemetryHud() const { return *m_pTelemetry; }
    class InputHud& getInputHud() const { return *m_pInput; }
    class TimeWidget& getTimeWidget() const { return *m_pTime; }
    class PositionWidget& getPositionWidget() const { return *m_pPosition; }
    class LapWidget& getLapWidget() const { return *m_pLap; }
    class SessionWidget& getSessionWidget() const { return *m_pSession; }
    class MapHud& getMapHud() const { return *m_pMapHud; }
    class RadarHud& getRadarHud() const { return *m_pRadarHud; }
    class SpeedWidget& getSpeedWidget() const { return *m_pSpeed; }
    class SpeedoWidget& getSpeedoWidget() const { return *m_pSpeedo; }
    class TachoWidget& getTachoWidget() const { return *m_pTacho; }
    class TimingHud& getTimingHud() const { return *m_pTiming; }
    class BarsWidget& getBarsWidget() const { return *m_pBars; }
    class VersionWidget& getVersionWidget() const { return *m_pVersion; }
    class NoticesWidget& getNoticesWidget() const { return *m_pNotices; }
    class SettingsButtonWidget& getSettingsButtonWidget() const { return *m_pSettingsButton; }
    class PitboardHud& getPitboardHud() const { return *m_pPitboard; }
    class RecordsHud& getRecordsHud() const { return *m_pRecords; }
    class FuelWidget& getFuelWidget() const { return *m_pFuel; }
    class GapBarHud& getGapBarHud() const { return *m_pGapBar; }
    class PointerWidget& getPointerWidget() const { return *m_pPointer; }
    class RumbleHud& getRumbleHud() const { return *m_pRumble; }

private:
    HudManager() : m_bInitialized(false), m_bResourcesInitialized(false),
                   m_pDraggingHud(nullptr), m_pSettingsHud(nullptr), m_pSettingsButton(nullptr),
                   m_pSessionBest(nullptr), m_pLapLog(nullptr), m_pStandings(nullptr),
                   m_pPerformance(nullptr), m_pTelemetry(nullptr),
                   m_pInput(nullptr), m_pTime(nullptr), m_pPosition(nullptr), m_pLap(nullptr), m_pSession(nullptr), m_pMapHud(nullptr), m_pRadarHud(nullptr), m_pSpeed(nullptr), m_pSpeedo(nullptr), m_pTacho(nullptr), m_pTiming(nullptr), m_pGapBar(nullptr), m_pBars(nullptr), m_pVersion(nullptr), m_pNotices(nullptr), m_pPitboard(nullptr), m_pRecords(nullptr), m_pFuel(nullptr), m_pPointer(nullptr), m_pRumble(nullptr),
                   m_numSpriteNames(0), m_numFontNames(0),
                   m_bAllHudsToggledOff(false), m_bAllWidgetsToggledOff(false) {
        m_spriteBuffer[0] = '\0';
        m_fontBuffer[0] = '\0';
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
    class SessionBestHud* m_pSessionBest;
    class LapLogHud* m_pLapLog;
    class StandingsHud* m_pStandings;
    class PerformanceHud* m_pPerformance;
    class TelemetryHud* m_pTelemetry;
    class InputHud* m_pInput;
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

    // Temporary HUD visibility toggle (doesn't modify actual visibility state)
    bool m_bAllHudsToggledOff;
    bool m_bAllWidgetsToggledOff;

    // Collected render data from all HUDs
    std::vector<SPluginQuad_t> m_quads;
    std::vector<SPluginString_t> m_strings;

    // Resource management - using fixed-size buffers instead of std::string
    static constexpr size_t MAX_SPRITE_NAMES = 29;  // Increased for rumble HUD texture
    static constexpr size_t MAX_FONT_NAMES = 10;
    static constexpr size_t MAX_NAME_LENGTH = 256;

    char m_spriteNames[MAX_SPRITE_NAMES][MAX_NAME_LENGTH];
    char m_fontNames[MAX_FONT_NAMES][MAX_NAME_LENGTH];
    int m_numSpriteNames;
    int m_numFontNames;

    char m_spriteBuffer[MAX_SPRITE_NAMES * MAX_NAME_LENGTH];  // Null-separated sprite names
    char m_fontBuffer[MAX_FONT_NAMES * MAX_NAME_LENGTH];      // Null-separated font names

    // Performance optimization constants
    // Set to typical first-frame usage to avoid multiple reallocations
    // PerformanceHud: ~250 quads, MapHud: ~700 quads, StandingsHud: 2 quads + ~90 strings
    static constexpr size_t INITIAL_QUAD_CAPACITY = 2000;  // Accounts for map + performance graphs
    static constexpr size_t INITIAL_STRING_CAPACITY = 400;  // Accounts for standings + labels
    static constexpr size_t CAPACITY_GROWTH_FACTOR = 2;
};
