// ============================================================================
// core/hud_manager.cpp
// Manages all HUD display elements and coordinates their rendering and updates
// ============================================================================
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "plugin_data.h"
#include "plugin_manager.h"
#include "settings_manager.h"
#include "../hud/base_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/input_hud.h"
#include "../hud/session_best_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_widget.h"
#include "../hud/speed_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_widget.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_widget.h"
#include "../hud/settings_hud.h"
#include "../hud/settings_button_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/fuel_widget.h"
#include "../hud/records_hud.h"
#include "../hud/cursor.h"
#include <windows.h>
#include <memory>
#include <cstring>

HudManager& HudManager::getInstance() {
    static HudManager instance;
    return instance;
}

HudManager::~HudManager() {
    shutdown();
}

void HudManager::initialize() {
    if (m_bInitialized) return;

    DEBUG_INFO("HudManager initializing");

    // Pre-allocate render data vectors for optimal performance
    m_quads.reserve(INITIAL_QUAD_CAPACITY);
    m_strings.reserve(INITIAL_STRING_CAPACITY);

    // Setup default resources (this prepares the resource lists)
    setupDefaultResources();

    // Register HUDs
    // Capture pointers to HUDs for SettingsHud and settings persistence
    // Order matches settings tabs for consistency
    auto standingsPtr = std::make_unique<StandingsHud>();
    m_pStandings = standingsPtr.get();
    m_pStandings->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_STANDINGS_HUD);
    registerHud(std::move(standingsPtr));

    auto mapPtr = std::make_unique<MapHud>();
    m_pMapHud = mapPtr.get();
    m_pMapHud->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_MAP_HUD);
    registerHud(std::move(mapPtr));

    auto radarPtr = std::make_unique<RadarHud>();
    m_pRadarHud = radarPtr.get();
    m_pRadarHud->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_RADAR_HUD);
    registerHud(std::move(radarPtr));

    auto lapLogPtr = std::make_unique<LapLogHud>();
    m_pLapLog = lapLogPtr.get();
    m_pLapLog->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_LAP_LOG_HUD);
    registerHud(std::move(lapLogPtr));

    auto sessionBestPtr = std::make_unique<SessionBestHud>();
    m_pSessionBest = sessionBestPtr.get();
    m_pSessionBest->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_SESSION_BEST_HUD);
    registerHud(std::move(sessionBestPtr));

    auto telemetryPtr = std::make_unique<TelemetryHud>();
    m_pTelemetry = telemetryPtr.get();
    m_pTelemetry->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_TELEMETRY_HUD);
    registerHud(std::move(telemetryPtr));

    auto inputPtr = std::make_unique<InputHud>();
    m_pInput = inputPtr.get();
    m_pInput->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_INPUT_HUD);
    registerHud(std::move(inputPtr));

    auto performancePtr = std::make_unique<PerformanceHud>();
    m_pPerformance = performancePtr.get();
    m_pPerformance->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_PERFORMANCE_HUD);
    registerHud(std::move(performancePtr));

    auto pitboardPtr = std::make_unique<PitboardHud>();
    m_pPitboard = pitboardPtr.get();
    m_pPitboard->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_PITBOARD_HUD);
    registerHud(std::move(pitboardPtr));

    auto recordsPtr = std::make_unique<RecordsHud>();
    m_pRecords = recordsPtr.get();
    m_pRecords->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_RECORDS_HUD);
    registerHud(std::move(recordsPtr));

    // Widgets
    auto lapPtr = std::make_unique<LapWidget>();
    m_pLap = lapPtr.get();
    m_pLap->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_LAP_WIDGET);
    registerHud(std::move(lapPtr));

    auto positionPtr = std::make_unique<PositionWidget>();
    m_pPosition = positionPtr.get();
    m_pPosition->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_POSITION_WIDGET);
    registerHud(std::move(positionPtr));

    auto timePtr = std::make_unique<TimeWidget>();
    m_pTime = timePtr.get();
    m_pTime->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_TIME_WIDGET);
    registerHud(std::move(timePtr));

    auto sessionPtr = std::make_unique<SessionWidget>();
    m_pSession = sessionPtr.get();
    m_pSession->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_SESSION_WIDGET);
    registerHud(std::move(sessionPtr));

    auto speedPtr = std::make_unique<SpeedWidget>();
    m_pSpeed = speedPtr.get();
    m_pSpeed->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_SPEED_WIDGET);
    registerHud(std::move(speedPtr));

    auto speedoPtr = std::make_unique<SpeedoWidget>();
    m_pSpeedo = speedoPtr.get();
    m_pSpeedo->setBackgroundTextureIndex(PluginConstants::SpriteIndex::SPEEDO_DIAL);
    registerHud(std::move(speedoPtr));

    auto tachoPtr = std::make_unique<TachoWidget>();
    m_pTacho = tachoPtr.get();
    m_pTacho->setBackgroundTextureIndex(PluginConstants::SpriteIndex::TACHO_DIAL);
    registerHud(std::move(tachoPtr));

    auto timingPtr = std::make_unique<TimingWidget>();
    m_pTiming = timingPtr.get();
    m_pTiming->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_TIMING_WIDGET);
    registerHud(std::move(timingPtr));

    auto barsPtr = std::make_unique<BarsWidget>();
    m_pBars = barsPtr.get();
    m_pBars->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_BARS_WIDGET);
    registerHud(std::move(barsPtr));

    auto versionPtr = std::make_unique<VersionWidget>();
    m_pVersion = versionPtr.get();
    registerHud(std::move(versionPtr));

    auto noticesPtr = std::make_unique<NoticesWidget>();
    m_pNotices = noticesPtr.get();
    registerHud(std::move(noticesPtr));

    auto fuelPtr = std::make_unique<FuelWidget>();
    m_pFuel = fuelPtr.get();
    m_pFuel->setBackgroundTextureIndex(PluginConstants::SpriteIndex::BG_FUEL_WIDGET);
    registerHud(std::move(fuelPtr));

    // Register SettingsHud with pointers to all configurable HUDs and widgets
    auto settingsPtr = std::make_unique<SettingsHud>(m_pSessionBest, m_pLapLog, m_pStandings,
                                                       m_pPerformance, m_pTelemetry, m_pInput, m_pTime, m_pPosition, m_pLap, m_pSession, m_pMapHud, m_pRadarHud, m_pSpeed, m_pSpeedo, m_pTacho, m_pTiming, m_pBars, m_pVersion, m_pNotices, m_pPitboard, m_pRecords, m_pFuel);
    m_pSettingsHud = settingsPtr.get();
    registerHud(std::move(settingsPtr));

    // Register SettingsButtonWidget - draggable button to toggle settings
    auto settingsButtonPtr = std::make_unique<SettingsButtonWidget>();
    m_pSettingsButton = settingsButtonPtr.get();
    registerHud(std::move(settingsButtonPtr));

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
    if (!m_bInitialized) return;

    DEBUG_INFO("HudManager shutting down");

    // Save settings before clearing HUDs
    SettingsManager::getInstance().saveSettings(*this, PluginManager::getInstance().getSavePath());

    clear();

    m_bInitialized = false;
    m_bResourcesInitialized = false;
    DEBUG_INFO("HudManager shutdown complete");
}

void HudManager::clear() {
    // Reset cached HUD pointers BEFORE destroying the objects
    // This prevents any dangling pointer window (defensive programming)
    m_pSessionBest = nullptr;
    m_pLapLog = nullptr;
    m_pStandings = nullptr;
    m_pPerformance = nullptr;
    m_pTelemetry = nullptr;
    m_pInput = nullptr;
    m_pTime = nullptr;
    m_pPosition = nullptr;
    m_pLap = nullptr;
    m_pSession = nullptr;
    m_pMapHud = nullptr;
    m_pRadarHud = nullptr;
    m_pSpeed = nullptr;
    m_pSpeedo = nullptr;
    m_pTiming = nullptr;
    m_pNotices = nullptr;
    m_pPitboard = nullptr;
    m_pRecords = nullptr;
    m_pFuel = nullptr;
    m_pSettingsHud = nullptr;
    m_pSettingsButton = nullptr;
    m_pDraggingHud = nullptr;

    // Now safe to destroy HUD objects
    m_huds.clear();
    m_quads.clear();
    m_strings.clear();

    // Clean up resource name storage
    m_numSpriteNames = 0;
    m_numFontNames = 0;
    m_spriteBuffer[0] = '\0';
    m_fontBuffer[0] = '\0';

    DEBUG_INFO("HudManager data cleared");
}

int HudManager::initializeResources(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName) {
    if (m_bResourcesInitialized) {
        DEBUG_WARN("HudManager resources already initialized");
        return 0;
    }

    DEBUG_INFO("HudManager initializing resources");

    // Build null-separated sprite names buffer
    char* bufferPos = m_spriteBuffer;
    size_t remainingSpace = sizeof(m_spriteBuffer);

    for (int i = 0; i < m_numSpriteNames; ++i) {
        size_t nameLen = strlen(m_spriteNames[i]);
        if (nameLen + 1 > remainingSpace) break;  // +1 for null terminator

        memcpy(bufferPos, m_spriteNames[i], nameLen + 1);
        bufferPos += nameLen + 1;
        remainingSpace -= nameLen + 1;
    }

    // Build null-separated font names buffer  
    bufferPos = m_fontBuffer;
    remainingSpace = sizeof(m_fontBuffer);

    for (int i = 0; i < m_numFontNames; ++i) {
        size_t nameLen = strlen(m_fontNames[i]);
        if (nameLen + 1 > remainingSpace) break;  // +1 for null terminator

        memcpy(bufferPos, m_fontNames[i], nameLen + 1);
        bufferPos += nameLen + 1;
        remainingSpace -= nameLen + 1;
    }

    // Set output parameters
    *piNumSprites = m_numSpriteNames;
    *pszSpriteName = (m_numSpriteNames > 0) ? m_spriteBuffer : nullptr;

    *piNumFonts = m_numFontNames;
    *pszFontName = (m_numFontNames > 0) ? m_fontBuffer : nullptr;

    m_bResourcesInitialized = true;

    DEBUG_INFO_F("Resources initialized: %d sprites, %d fonts", m_numSpriteNames, m_numFontNames);

    for (int i = 0; i < m_numSpriteNames; ++i) {
        DEBUG_INFO_F("Sprite: %s", m_spriteNames[i]);
    }

    for (int i = 0; i < m_numFontNames; ++i) {
        DEBUG_INFO_F("Font: %s", m_fontNames[i]);
    }

    return 0;
}

void HudManager::registerHud(std::unique_ptr<BaseHud> hud) {
    if (hud) {
        m_huds.push_back(std::move(hud));
        DEBUG_INFO_F("HUD registered, total HUDs: %zu", m_huds.size());
    }
}

void HudManager::onDataChanged(DataChangeType changeType) {

    // Called when PluginData notifies that data has changed
    // Mark relevant HUDs as dirty based on data type
    for (auto& hud : m_huds) {
        if (hud && hud->handlesDataType(changeType)) {
            hud->setDataDirty();
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

void HudManager::draw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString) {

    if (!m_bInitialized) {
        *piNumQuads = 0;
        *piNumString = 0;
        return;
    }

    // Update input data once per frame at the beginning
    InputManager::getInstance().updateFrame();

    // Update all HUDs (they will only rebuild if marked dirty)
    updateHuds();

    // Collect render data from all HUDs
    collectRenderData();

    // Add cursor quad (if cursor should be visible)
    CursorRenderer::addCursorQuad(m_quads);

    // Set output parameters
    *piNumQuads = static_cast<int>(m_quads.size());
    *ppQuad = m_quads.empty() ? nullptr : m_quads.data();

    *piNumString = static_cast<int>(m_strings.size());
    *ppString = m_strings.empty() ? nullptr : m_strings.data();
}

void HudManager::updateHuds() {
    // Handle settings button click for SettingsHud toggle
    handleSettingsButton();

    // Handle keyboard shortcuts for HUD toggles
    processKeyboardInput();

    // Only allow one HUD to be dragged at a time
    // Process HUDs in reverse order (last registered = top layer, gets priority)

    // Use cached dragging HUD if valid, otherwise find new target
    BaseHud* inputTarget = nullptr;

    // Check if cached dragging HUD is still valid and dragging
    if (m_pDraggingHud && m_pDraggingHud->isDragging()) {
        inputTarget = m_pDraggingHud;
    }
    else {
        // Clear cached pointer if drag ended
        m_pDraggingHud = nullptr;

        // Find new target on click (not every frame)
        const InputManager& input = InputManager::getInstance();
        const MouseButton& leftButton = input.getLeftButton();
        const MouseButton& rightButton = input.getRightButton();

        // Check for either LMB (for clicks) or RMB (for dragging)
        if ((leftButton.isClicked() || rightButton.isClicked()) && input.isCursorEnabled()) {
            const CursorPosition& cursor = input.getCursorPosition();
            if (cursor.isValid) {
                for (auto it = m_huds.rbegin(); it != m_huds.rend(); ++it) {
                    auto& hud = *it;
                    if (hud && hud->isDraggable() && hud->isVisible()) {
                        // Check bounds directly to avoid calling handleMouseInput twice
                        if (hud->isPointInBounds(cursor.x, cursor.y)) {
                            inputTarget = hud.get();
                            break;
                        }
                    }
                }
            }
        }
    }

    // Now update all HUDs
    for (auto& hud : m_huds) {
        if (hud) {
            // Allow mouse input only for the target HUD (and only if visible)
            bool allowInput = (hud.get() == inputTarget);

            if (hud->isDraggable() && hud->isVisible()) {
                hud->handleMouseInput(allowInput);

                // Cache the HUD if it just started dragging
                if (hud->isDragging() && !m_pDraggingHud) {
                    m_pDraggingHud = hud.get();
                }
            }

            // Always call update() to handle data/layout dirty flags
            hud->update();
        }
    }
}

void HudManager::collectRenderData() {

    // Calculate total capacity needed to minimize allocations
    size_t totalQuads = 0;
    size_t totalStrings = 0;

    for (const auto& hud : m_huds) {
        if (hud) {
            totalQuads += hud->getQuads().size();
            totalStrings += hud->getStrings().size();
        }
    }

    // Reserve space for cursor and settings button (always allocated, conditionally added)
    totalQuads += 2;     // Cursor quad + settings button background quad
    totalStrings += 1;   // Settings button text

    // Ensure vectors have sufficient capacity - grow if needed but never shrink
    if (m_quads.capacity() < totalQuads) {
        size_t newCapacity = totalQuads * CAPACITY_GROWTH_FACTOR;
        m_quads.reserve(newCapacity);
        DEBUG_INFO_F("HudManager quads capacity increased to %zu", newCapacity);
    }

    if (m_strings.capacity() < totalStrings) {
        size_t newCapacity = totalStrings * CAPACITY_GROWTH_FACTOR;
        m_strings.reserve(newCapacity);
        DEBUG_INFO_F("HudManager strings capacity increased to %zu", newCapacity);
    }

    // Clear existing data but keep allocated memory
    m_quads.resize(0);
    m_strings.resize(0);

    // Collect from all visible HUDs using efficient vector operations
    // Settings and settings button are always rendered (even when toggle key pressed)
    for (const auto& hud : m_huds) {
        if (hud && hud->isVisible()) {
            // Skip rendering if temporary toggle is active (except settings HUDs)
            bool isSettingsHud = (hud.get() == m_pSettingsHud || hud.get() == m_pSettingsButton);
            if (m_bAllHudsToggledOff && !isSettingsHud) {
                continue;
            }

            // Skip rendering widgets if widget toggle is active
            bool isWidget = (hud.get() == m_pLap || hud.get() == m_pPosition ||
                           hud.get() == m_pTime || hud.get() == m_pSession ||
                           hud.get() == m_pSpeed || hud.get() == m_pSpeedo ||
                           hud.get() == m_pTacho || hud.get() == m_pTiming ||
                           hud.get() == m_pBars || hud.get() == m_pVersion ||
                           hud.get() == m_pNotices || hud.get() == m_pFuel);
            if (m_bAllWidgetsToggledOff && isWidget) {
                continue;
            }

            const auto& hudQuads = hud->getQuads();
            const auto& hudStrings = hud->getStrings();

            // Use insert with iterators for efficient bulk copy
            m_quads.insert(m_quads.end(), hudQuads.begin(), hudQuads.end());
            m_strings.insert(m_strings.end(), hudStrings.begin(), hudStrings.end());
        }
    }
}

void HudManager::setupDefaultResources() {
    // Initialize counters
    m_numSpriteNames = 0;
    m_numFontNames = 0;

    // Helper lambda to add sprite with bounds checking
    auto addSprite = [this](const char* path) {
        if (m_numSpriteNames < MAX_SPRITE_NAMES) {
            strcpy_s(m_spriteNames[m_numSpriteNames++], MAX_NAME_LENGTH, path);
        } else {
            DEBUG_WARN_F("MAX_SPRITE_NAMES exceeded, cannot add %s", path);
        }
    };

    // Add default sprites needed by HUDs (including cursor)
    // IMPORTANT: Sprite order must match SpriteIndex constants in plugin_constants.h
    // Index 1: pointer, Index 2: gear-circle, Index 3+: background textures
    addSprite("mxbmrp3_data\\pointer.tga");          // SpriteIndex::POINTER = 1
    addSprite("mxbmrp3_data\\gear-circle.tga");      // SpriteIndex::GEAR_CIRCLE = 2

    // Add HUD/widget background texture sprites (optional - files may not exist)
    // These map to SpriteIndex::BG_* constants (3-16)
    addSprite("mxbmrp3_data\\standings_hud.tga");    // SpriteIndex::BG_STANDINGS_HUD = 3
    addSprite("mxbmrp3_data\\map_hud.tga");          // SpriteIndex::BG_MAP_HUD = 4
    addSprite("mxbmrp3_data\\lap_log_hud.tga");      // SpriteIndex::BG_LAP_LOG_HUD = 5
    addSprite("mxbmrp3_data\\session_best_hud.tga"); // SpriteIndex::BG_SESSION_BEST_HUD = 6
    addSprite("mxbmrp3_data\\telemetry_hud.tga");    // SpriteIndex::BG_TELEMETRY_HUD = 7
    addSprite("mxbmrp3_data\\input_hud.tga");        // SpriteIndex::BG_INPUT_HUD = 8
    addSprite("mxbmrp3_data\\performance_hud.tga");  // SpriteIndex::BG_PERFORMANCE_HUD = 9
    addSprite("mxbmrp3_data\\lap_widget.tga");       // SpriteIndex::BG_LAP_WIDGET = 10
    addSprite("mxbmrp3_data\\position_widget.tga");  // SpriteIndex::BG_POSITION_WIDGET = 11
    addSprite("mxbmrp3_data\\time_widget.tga");      // SpriteIndex::BG_TIME_WIDGET = 12
    addSprite("mxbmrp3_data\\session_widget.tga");   // SpriteIndex::BG_SESSION_WIDGET = 13
    addSprite("mxbmrp3_data\\speed_widget.tga");     // SpriteIndex::BG_SPEED_WIDGET = 14
    addSprite("mxbmrp3_data\\timing_widget.tga");    // SpriteIndex::BG_TIMING_WIDGET = 15
    addSprite("mxbmrp3_data\\bars_widget.tga");      // SpriteIndex::BG_BARS_WIDGET = 16
    addSprite("mxbmrp3_data\\pitboard_hud.tga");    // SpriteIndex::BG_PITBOARD_HUD = 17
    addSprite("mxbmrp3_data\\speedo_widget.tga"); // SpriteIndex::SPEEDO_DIAL = 18
    addSprite("mxbmrp3_data\\tacho_widget.tga");  // SpriteIndex::TACHO_DIAL = 19
    addSprite("mxbmrp3_data\\radar_hud.tga");     // SpriteIndex::BG_RADAR_HUD = 20
    addSprite("mxbmrp3_data\\radar_sector.tga"); // SpriteIndex::RADAR_SECTOR = 21
    addSprite("mxbmrp3_data\\fuel_widget.tga");  // SpriteIndex::BG_FUEL_WIDGET = 22
    addSprite("mxbmrp3_data\\records_hud.tga");  // SpriteIndex::BG_RECORDS_HUD = 23

    // Add default fonts needed by HUDs
    // Safety: Check array bounds before incrementing to prevent buffer overflow
    if (m_numFontNames < MAX_FONT_NAMES) {
        strcpy_s(m_fontNames[m_numFontNames++], MAX_NAME_LENGTH, "mxbmrp3_data\\EnterSansman-Italic.fnt");
    } else {
        DEBUG_WARN("MAX_FONT_NAMES exceeded, cannot add EnterSansman-Italic.fnt");
    }

    if (m_numFontNames < MAX_FONT_NAMES) {
        strcpy_s(m_fontNames[m_numFontNames++], MAX_NAME_LENGTH, "mxbmrp3_data\\RobotoMono-Regular.fnt");
    } else {
        DEBUG_WARN("MAX_FONT_NAMES exceeded, cannot add RobotoMono-Regular.fnt");
    }

    if (m_numFontNames < MAX_FONT_NAMES) {
        strcpy_s(m_fontNames[m_numFontNames++], MAX_NAME_LENGTH, "mxbmrp3_data\\RobotoMono-Bold.fnt");
    } else {
        DEBUG_WARN("MAX_FONT_NAMES exceeded, cannot add RobotoMono-Bold.fnt");
    }

    if (m_numFontNames < MAX_FONT_NAMES) {
        strcpy_s(m_fontNames[m_numFontNames++], MAX_NAME_LENGTH, "mxbmrp3_data\\FuzzyBubbles-Regular.fnt");
    } else {
        DEBUG_WARN("MAX_FONT_NAMES exceeded, cannot add FuzzyBubbles-Regular.fnt");
    }

    if (m_numFontNames < MAX_FONT_NAMES) {
        strcpy_s(m_fontNames[m_numFontNames++], MAX_NAME_LENGTH, "mxbmrp3_data\\Tiny5-Regular.fnt");
    } else {
        DEBUG_WARN("MAX_FONT_NAMES exceeded, cannot add Tiny5-Regular.fnt");
    }

    DEBUG_INFO("Default HUD resources configured");
}

void HudManager::handleSettingsButton() {
    if (!m_pSettingsHud || !m_pSettingsButton) return;

    // Check if settings button was clicked
    if (m_pSettingsButton->isClicked()) {
        // Toggle SettingsHud visibility
        if (m_pSettingsHud->isVisible()) {
            m_pSettingsHud->hide();
            DEBUG_INFO("SettingsHud hidden (button clicked)");
        } else {
            m_pSettingsHud->show();
            DEBUG_INFO("SettingsHud shown (button clicked)");
        }
    }
}

void HudManager::processKeyboardInput() {
    const InputManager& input = InputManager::getInstance();

    // `~ or \| key: Toggle settings menu (or Ctrl+key for HUD visibility)
    // Process this BEFORE modifier key check to allow Ctrl+OEM3/5
    // Supports both VK_OEM_3 (`~ on US, § on some EU) and VK_OEM_5 (\| on US)
    if (input.getOem3Key().isClicked() || input.getOem5Key().isClicked()) {
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        if (ctrlPressed) {
            // Ctrl+`~ or Ctrl+\|: Temporary toggle all HUDs on/off (doesn't modify actual visibility state)
            m_bAllHudsToggledOff = !m_bAllHudsToggledOff;
            DEBUG_INFO_F("Ctrl+Toggle key: All HUDs temporarily %s", m_bAllHudsToggledOff ? "hidden" : "shown");
        } else {
            // `~ or \| alone: Toggle settings menu
            if (m_pSettingsHud) {
                if (m_pSettingsHud->isVisible()) {
                    m_pSettingsHud->hide();
                    DEBUG_INFO("Toggle key: Settings hidden");
                } else {
                    m_pSettingsHud->show();
                    DEBUG_INFO("Toggle key: Settings shown");
                }
            }
        }
        return;  // Don't process other shortcuts after handling OEM keys
    }

    // Ignore keyboard shortcuts when modifier keys are pressed (Shift, Ctrl, Alt)
    // This prevents conflicts with system shortcuts like Alt+Tab, Shift+F4, etc.
    if (input.isAnyModifierKeyPressed()) {
        return;
    }

    // F1-F8: Toggle individual HUDs
    if (input.getF1Key().isClicked() && m_pStandings) {
        m_pStandings->setVisible(!m_pStandings->isVisible());
        DEBUG_INFO_F("F1: Standings %s", m_pStandings->isVisible() ? "shown" : "hidden");
    }

    if (input.getF2Key().isClicked() && m_pMapHud) {
        m_pMapHud->setVisible(!m_pMapHud->isVisible());
        DEBUG_INFO_F("F2: Map %s", m_pMapHud->isVisible() ? "shown" : "hidden");
    }

    if (input.getF3Key().isClicked() && m_pRadarHud) {
        m_pRadarHud->setVisible(!m_pRadarHud->isVisible());
        DEBUG_INFO_F("F3: Radar %s", m_pRadarHud->isVisible() ? "shown" : "hidden");
    }

    if (input.getF4Key().isClicked() && m_pLapLog) {
        m_pLapLog->setVisible(!m_pLapLog->isVisible());
        DEBUG_INFO_F("F4: Lap Log %s", m_pLapLog->isVisible() ? "shown" : "hidden");
    }

    if (input.getF5Key().isClicked() && m_pSessionBest) {
        m_pSessionBest->setVisible(!m_pSessionBest->isVisible());
        DEBUG_INFO_F("F5: Session Best %s", m_pSessionBest->isVisible() ? "shown" : "hidden");
    }

    if (input.getF6Key().isClicked() && m_pTelemetry) {
        m_pTelemetry->setVisible(!m_pTelemetry->isVisible());
        DEBUG_INFO_F("F6: Telemetry %s", m_pTelemetry->isVisible() ? "shown" : "hidden");
    }

    if (input.getF7Key().isClicked() && m_pInput) {
        m_pInput->setVisible(!m_pInput->isVisible());
        DEBUG_INFO_F("F7: Input %s", m_pInput->isVisible() ? "shown" : "hidden");
    }

    if (input.getF8Key().isClicked() && m_pRecords) {
        m_pRecords->setVisible(!m_pRecords->isVisible());
        DEBUG_INFO_F("F8: Records %s", m_pRecords->isVisible() ? "shown" : "hidden");
    }

    if (input.getF9Key().isClicked()) {
        // F9: Temporary toggle all widgets on/off (doesn't modify actual visibility state)
        m_bAllWidgetsToggledOff = !m_bAllWidgetsToggledOff;
        DEBUG_INFO_F("F9: Widgets temporarily %s", m_bAllWidgetsToggledOff ? "hidden" : "shown");
    }
}

bool HudManager::isSettingsVisible() const {
    return m_pSettingsHud && m_pSettingsHud->isVisible();
}

void HudManager::updateTrackCenterline(int numSegments, SPluginsTrackSegment_t* segments) {
    if (!m_bInitialized || !m_pMapHud) {
        DEBUG_WARN("HudManager: Cannot update track centerline - not initialized or MapHud not available");
        return;
    }

    DEBUG_INFO_F("HudManager: Updating track centerline with %d segments", numSegments);
    m_pMapHud->updateTrackData(numSegments, segments);
}

void HudManager::updateRiderPositions(int numVehicles, SPluginsRaceTrackPosition_t* positions) {
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
}
