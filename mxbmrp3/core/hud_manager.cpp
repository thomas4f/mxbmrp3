// ============================================================================
// core/hud_manager.cpp
// Manages all HUD display elements and coordinates their rendering and updates
// ============================================================================
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "asset_manager.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "plugin_data.h"
#include "plugin_manager.h"
#include "settings_manager.h"
#include "profile_manager.h"
#include "../hud/base_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_widget.h"
#include "../hud/speed_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
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
#include "../hud/gap_bar_hud.h"
#include "../hud/pointer_widget.h"
#include "../hud/rumble_hud.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "hotkey_manager.h"
#include "tooltip_manager.h"
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

    // Note: AssetManager::discoverAssets() is called by PluginManager before this

    // Pre-allocate render data vectors for optimal performance
    m_quads.reserve(INITIAL_QUAD_CAPACITY);
    m_strings.reserve(INITIAL_STRING_CAPACITY);

    // Setup default resources (this prepares the resource lists)
    setupDefaultResources();

    // Register HUDs
    // Capture pointers to HUDs for SettingsHud and settings persistence
    // Order matches settings tabs for consistency
    // Note: Texture base names match files in mxbmrp3_data/textures/ (e.g., "standings_hud" for "standings_hud_1.tga")
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

    auto recordsPtr = std::make_unique<RecordsHud>();
    m_pRecords = recordsPtr.get();
    m_pRecords->setTextureBaseName("records_hud");
    registerHud(std::move(recordsPtr));

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

    auto sessionPtr = std::make_unique<SessionWidget>();
    m_pSession = sessionPtr.get();
    m_pSession->setTextureBaseName("session_widget");
    registerHud(std::move(sessionPtr));

    auto speedPtr = std::make_unique<SpeedWidget>();
    m_pSpeed = speedPtr.get();
    m_pSpeed->setTextureBaseName("speed_widget");
    registerHud(std::move(speedPtr));

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

    auto noticesPtr = std::make_unique<NoticesWidget>();
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

    auto gamepadPtr = std::make_unique<GamepadWidget>();
    m_pGamepad = gamepadPtr.get();
    m_pGamepad->setTextureBaseName("gamepad_widget");
    registerHud(std::move(gamepadPtr));

    auto leanPtr = std::make_unique<LeanWidget>();
    m_pLean = leanPtr.get();
    m_pLean->setTextureBaseName("lean_widget");
    registerHud(std::move(leanPtr));

    // Create PointerWidget early so it can be passed to SettingsHud
    // (will be registered last to render on top)
    auto pointerPtr = std::make_unique<PointerWidget>();
    m_pPointer = pointerPtr.get();

    // Register SettingsHud with pointers to all configurable HUDs and widgets
    auto settingsPtr = std::make_unique<SettingsHud>(m_pIdealLap, m_pLapLog, m_pStandings,
                                                       m_pPerformance, m_pTelemetry, m_pTime, m_pPosition, m_pLap, m_pSession, m_pMapHud, m_pRadarHud, m_pSpeed, m_pSpeedo, m_pTacho, m_pTiming, m_pGapBar, m_pBars, m_pVersion, m_pNotices, m_pPitboard, m_pRecords, m_pFuel, m_pPointer, m_pRumble, m_pGamepad, m_pLean);
    m_pSettingsHud = settingsPtr.get();
    registerHud(std::move(settingsPtr));

    // Register SettingsButtonWidget - draggable button to toggle settings
    auto settingsButtonPtr = std::make_unique<SettingsButtonWidget>();
    m_pSettingsButton = settingsButtonPtr.get();
    registerHud(std::move(settingsButtonPtr));

    // Register PointerWidget last so it renders on top of everything
    registerHud(std::move(pointerPtr));

    // Load settings from disk (must happen after HUD registration)
    SettingsManager::getInstance().loadSettings(*this, PluginManager::getInstance().getSavePath());

    // Load UI descriptions for settings panel
    TooltipManager::getInstance().load();

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
    m_pIdealLap = nullptr;
    m_pLapLog = nullptr;
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
    m_pSpeedo = nullptr;
    m_pTiming = nullptr;
    m_pNotices = nullptr;
    m_pPitboard = nullptr;
    m_pRecords = nullptr;
    m_pFuel = nullptr;
    m_pRumble = nullptr;
    m_pGamepad = nullptr;
    m_pLean = nullptr;
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

void HudManager::onDataChanged(DataChangeType changeType) {

    // Called when PluginData notifies that data has changed
    // Mark relevant HUDs as dirty based on data type
    for (auto& hud : m_huds) {
        if (hud && hud->handlesDataType(changeType)) {
            hud->setDataDirty();
        }
    }

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

            // If target differs from current, switch profiles
            if (targetProfile != profileMgr.getActiveProfile()) {
                SettingsManager::getInstance().switchProfile(*this, targetProfile);
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

void HudManager::draw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString) {

    if (!m_bInitialized) {
        *piNumQuads = 0;
        *piNumString = 0;
        return;
    }

    // Update input data once per frame at the beginning
    InputManager::getInstance().updateFrame();

    // Update hotkey manager (checks for triggered actions)
    HotkeyManager::getInstance().update();

    // Update all HUDs (they will only rebuild if marked dirty)
    updateHuds();

    // Collect render data from all HUDs
    // Note: PointerWidget is registered last, so pointer renders on top
    collectRenderData();

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

    // Reserve space for pointer and settings button (always allocated, conditionally added)
    totalQuads += 2;     // Pointer quad + settings button background quad
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
            // Check if version widget's easter egg game is active (bypasses all toggles)
            bool isVersionGameActive = (hud.get() == m_pVersion && m_pVersion && m_pVersion->isGameActive());

            // Skip rendering if temporary toggle is active (except settings HUDs, pointer, and active game)
            bool isSettingsHud = (hud.get() == m_pSettingsHud || hud.get() == m_pSettingsButton);
            bool isPointer = (hud.get() == m_pPointer);
            if (m_bAllHudsToggledOff && !isSettingsHud && !isPointer && !isVersionGameActive) {
                continue;
            }

            // Skip rendering widgets if widget toggle is active
            bool isWidget = (hud.get() == m_pLap || hud.get() == m_pPosition ||
                           hud.get() == m_pTime || hud.get() == m_pSession ||
                           hud.get() == m_pSpeed || hud.get() == m_pSpeedo ||
                           hud.get() == m_pTacho ||
                           hud.get() == m_pBars || hud.get() == m_pVersion ||
                           hud.get() == m_pNotices || hud.get() == m_pFuel ||
                           hud.get() == m_pGamepad || hud.get() == m_pLean);
            if (m_bAllWidgetsToggledOff && isWidget && !isVersionGameActive) {
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
    // Skip hotkey processing if in capture mode or if capture just completed this frame
    // Use didCaptureCompleteThisFrame() to avoid consuming the flag (settings UI needs it)
    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    if (hotkeyMgr.isCapturing() || hotkeyMgr.didCaptureCompleteThisFrame()) {
        return;
    }

    // Settings toggle - handle based on configured key
    const HotkeyBinding& settingsBinding = hotkeyMgr.getBinding(HotkeyAction::TOGGLE_SETTINGS);
    uint8_t configuredKey = settingsBinding.keyboard.keyCode;
    bool settingsTriggered = false;

    if ((configuredKey == VK_OEM_3 || configuredKey == VK_OEM_5) &&
        settingsBinding.keyboard.modifiers == ModifierFlags::NONE) {
        // For ` and \ keys without modifiers, use InputManager directly (handles keyboard layout differences)
        // Check both keys as fallback, but only trigger if no modifiers are held
        bool noModifiers = !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                           !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                           !(GetAsyncKeyState(VK_MENU) & 0x8000);  // VK_MENU = Alt
        const InputManager& input = InputManager::getInstance();
        if (noModifiers &&
            (input.getOem3Key().isClicked() || input.getOem5Key().isClicked())) {
            settingsTriggered = true;
        }
    } else if (configuredKey != 0) {
        // For other keys, use HotkeyManager
        settingsTriggered = hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_SETTINGS);
    }
    // If cleared (configuredKey == 0), nothing triggers

    if (settingsTriggered && m_pSettingsHud) {
        if (m_pSettingsHud->isVisible()) {
            m_pSettingsHud->hide();
            DEBUG_INFO("Hotkey: Settings hidden");
        } else {
            m_pSettingsHud->show();
            DEBUG_INFO("Hotkey: Settings shown");
        }
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_ALL_HUDS)) {
        m_bAllHudsToggledOff = !m_bAllHudsToggledOff;
        DEBUG_INFO_F("Hotkey: All HUDs temporarily %s", m_bAllHudsToggledOff ? "hidden" : "shown");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_STANDINGS) && m_pStandings) {
        m_pStandings->setVisible(!m_pStandings->isVisible());
        DEBUG_INFO_F("Hotkey: Standings %s", m_pStandings->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_MAP) && m_pMapHud) {
        m_pMapHud->setVisible(!m_pMapHud->isVisible());
        DEBUG_INFO_F("Hotkey: Map %s", m_pMapHud->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RADAR) && m_pRadarHud) {
        m_pRadarHud->setVisible(!m_pRadarHud->isVisible());
        DEBUG_INFO_F("Hotkey: Radar %s", m_pRadarHud->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_LAP_LOG) && m_pLapLog) {
        m_pLapLog->setVisible(!m_pLapLog->isVisible());
        DEBUG_INFO_F("Hotkey: Lap Log %s", m_pLapLog->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_IDEAL_LAP) && m_pIdealLap) {
        m_pIdealLap->setVisible(!m_pIdealLap->isVisible());
        DEBUG_INFO_F("Hotkey: Ideal Lap %s", m_pIdealLap->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_TELEMETRY) && m_pTelemetry) {
        m_pTelemetry->setVisible(!m_pTelemetry->isVisible());
        DEBUG_INFO_F("Hotkey: Telemetry %s", m_pTelemetry->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_INPUT) && m_pGamepad) {
        m_pGamepad->setVisible(!m_pGamepad->isVisible());
        DEBUG_INFO_F("Hotkey: Gamepad %s", m_pGamepad->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RECORDS) && m_pRecords) {
        m_pRecords->setVisible(!m_pRecords->isVisible());
        DEBUG_INFO_F("Hotkey: Records %s", m_pRecords->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_WIDGETS)) {
        m_bAllWidgetsToggledOff = !m_bAllWidgetsToggledOff;
        DEBUG_INFO_F("Hotkey: Widgets temporarily %s", m_bAllWidgetsToggledOff ? "hidden" : "shown");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_PITBOARD) && m_pPitboard) {
        m_pPitboard->setVisible(!m_pPitboard->isVisible());
        DEBUG_INFO_F("Hotkey: Pitboard %s", m_pPitboard->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_TIMING) && m_pTiming) {
        m_pTiming->setVisible(!m_pTiming->isVisible());
        DEBUG_INFO_F("Hotkey: Timing %s", m_pTiming->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_GAP_BAR) && m_pGapBar) {
        m_pGapBar->setVisible(!m_pGapBar->isVisible());
        DEBUG_INFO_F("Hotkey: Gap Bar %s", m_pGapBar->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_PERFORMANCE) && m_pPerformance) {
        m_pPerformance->setVisible(!m_pPerformance->isVisible());
        DEBUG_INFO_F("Hotkey: Performance %s", m_pPerformance->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RUMBLE) && m_pRumble) {
        m_pRumble->setVisible(!m_pRumble->isVisible());
        DEBUG_INFO_F("Hotkey: Rumble %s", m_pRumble->isVisible() ? "shown" : "hidden");
    }

    // Reload config from file
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::RELOAD_CONFIG)) {
        SettingsManager& settingsMgr = SettingsManager::getInstance();
        const std::string& savePath = settingsMgr.getSavePath();
        if (!savePath.empty()) {
            DEBUG_INFO("Hotkey: Reloading config from file");
            settingsMgr.loadSettings(*this, savePath.c_str());
            // Reload UI descriptions (hot-reload support)
            TooltipManager::getInstance().reload();
            // Mark all HUDs dirty to force rebuild
            if (m_pGamepad) m_pGamepad->setDataDirty();
            if (m_pSettingsHud) m_pSettingsHud->setDataDirty();
        }
    }

    // If any visibility toggle happened while settings is open, refresh it
    // All actions before TOGGLE_SETTINGS are visibility toggles
    if (m_pSettingsHud && m_pSettingsHud->isVisible()) {
        for (uint8_t i = 0; i < static_cast<uint8_t>(HotkeyAction::TOGGLE_SETTINGS); ++i) {
            if (hotkeyMgr.wasActionTriggered(static_cast<HotkeyAction>(i))) {
                m_pSettingsHud->setDataDirty();
                break;
            }
        }

        // Refresh when controller connection state changes
        if (XInputReader::getInstance().didConnectionStateChange()) {
            m_pSettingsHud->setDataDirty();
        }
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

    // Update centralized lap timer and HUDs with track position for S/F detection
    PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Find the display rider's position data
    for (int i = 0; i < numVehicles; ++i) {
        if (positions[i].m_iRaceNum == displayRaceNum) {
            // Get lap number from standings
            const StandingsData* standing = pluginData.getStanding(displayRaceNum);
            int lapNum = standing ? standing->numLaps : 0;

            // Update centralized lap timer (used by TimingHud, IdealLapHud, and others)
            pluginData.updateLapTimerTrackPosition(
                displayRaceNum,
                positions[i].m_fTrackPos,
                lapNum
            );

            // Update GapBarHud
            if (m_pGapBar) {
                m_pGapBar->updateTrackPosition(
                    displayRaceNum,
                    positions[i].m_fTrackPos,
                    lapNum
                );
            }
            break;
        }
    }
}
