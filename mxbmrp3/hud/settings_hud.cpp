// ============================================================================
// hud/settings_hud.cpp
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#include "settings_hud.h"
#include "telemetry_hud.h"
#include "rumble_hud.h"
#include "settings_button_widget.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/profile_manager.h"
#include "../core/update_checker.h"
#include "../core/hotkey_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include "../core/font_config.h"
#include "../core/plugin_data.h"
#include "../handlers/draw_handler.h"
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace PluginConstants;

// Helper template to cycle enum values forward or backward with wrap-around
// EnumT must be an enum class with sequential values starting from 0
template<typename EnumT>
EnumT cycleEnum(EnumT current, int enumCount, bool forward) {
    int val = static_cast<int>(current);
    if (forward) {
        val = (val + 1) % enumCount;
    } else {
        val = (val - 1 + enumCount) % enumCount;
    }
    return static_cast<EnumT>(val);
}

// Mode name lookup tables for debug output
static const char* getRiderColorModeName(int mode) {
    static const char* names[] = { "Uniform", "Brand", "Position" };
    return (mode >= 0 && mode < 3) ? names[mode] : "Unknown";
}

static const char* getLabelModeName(int mode) {
    static const char* names[] = { "None", "Position", "RaceNum", "Both" };
    return (mode >= 0 && mode < 4) ? names[mode] : "Unknown";
}

SettingsHud::SettingsHud(IdealLapHud* idealLap, LapLogHud* lapLog,
                         StandingsHud* standings,
                         PerformanceHud* performance,
                         TelemetryHud* telemetry, InputHud* input,
                         TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionWidget* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, SpeedoWidget* speedo, TachoWidget* tacho, TimingHud* timing, GapBarHud* gapBar, BarsWidget* bars, VersionWidget* version, NoticesWidget* notices, PitboardHud* pitboard, RecordsHud* records, FuelWidget* fuel, PointerWidget* pointer, RumbleHud* rumble)
    : m_idealLap(idealLap),
      m_lapLog(lapLog),
      m_standings(standings),
      m_performance(performance),
      m_telemetry(telemetry),
      m_input(input),
      m_time(time),
      m_position(position),
      m_lap(lap),
      m_session(session),
      m_mapHud(mapHud),
      m_radarHud(radarHud),
      m_speed(speed),
      m_speedo(speedo),
      m_tacho(tacho),
      m_timing(timing),
      m_gapBar(gapBar),
      m_bars(bars),
      m_version(version),
      m_notices(notices),
      m_pitboard(pitboard),
      m_records(records),
      m_fuel(fuel),
      m_pointer(pointer),
      m_rumble(rumble),
      m_bVisible(false),
      m_copyTargetProfile(-1),  // -1 = no target selected
      m_resetProfileConfirmed(false),
      m_resetAllConfirmed(false),
      m_checkForUpdates(false),
      m_updateStatus(UpdateStatus::UNKNOWN),
      m_latestVersion(""),
      m_cachedWindowWidth(0),
      m_cachedWindowHeight(0),
      m_activeTab(TAB_GENERAL),
      m_hoveredRegionIndex(-1),
      m_hoveredHotkeyRow(-1),
      m_hoveredHotkeyColumn(HotkeyColumn::NONE),
      m_hotkeyContentStartY(0.0f),
      m_hotkeyRowHeight(0.0f),
      m_hotkeyKeyboardX(0.0f),
      m_hotkeyControllerX(0.0f),
      m_hotkeyFieldCharWidth(0.0f),
      m_hoveredTrackedRiderIndex(-1),
      m_trackedRidersStartY(0.0f),
      m_trackedRidersCellHeight(0.0f),
      m_trackedRidersCellWidth(0.0f),
      m_trackedRidersStartX(0.0f),
      m_trackedRidersPerRow(0),
      m_serverPlayersPage(0),
      m_trackedRidersPage(0)
{
    DEBUG_INFO("SettingsHud created");
    setDraggable(true);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(60);  // Less text now with tabs
    m_clickRegions.reserve(60);  // Sized for largest tab (TAB_RIDERS has ~56 regions)

    // Start hidden
    hide();
}

void SettingsHud::show() {
    if (m_bVisible) return;

    m_bVisible = true;

    // Rebuild UI
    rebuildRenderData();
}

void SettingsHud::hide() {
    m_bVisible = false;
    m_strings.clear();
    m_quads.clear();
    m_clickRegions.clear();
    setBounds(0, 0, 0, 0);  // Clear collision bounds to prevent blocking input
}

void SettingsHud::update() {
    if (!m_bVisible) return;

    // Check for window resize (need to rebuild click regions with new coordinates)
    const InputManager& input = InputManager::getInstance();
    int currentWidth = input.getWindowWidth();
    int currentHeight = input.getWindowHeight();

    if (currentWidth != m_cachedWindowWidth || currentHeight != m_cachedWindowHeight) {
        // Window resized - rebuild everything to update click regions
        m_cachedWindowWidth = currentWidth;
        m_cachedWindowHeight = currentHeight;
        rebuildRenderData();
        DEBUG_INFO_F("SettingsHud rebuilt after window resize: %dx%d", currentWidth, currentHeight);
        return;  // Skip other processing this frame
    }

    // Track hover state for button backgrounds
    const CursorPosition& cursor = input.getCursorPosition();
    if (cursor.isValid) {
        int newHoveredIndex = -1;
        for (size_t i = 0; i < m_clickRegions.size(); ++i) {
            const auto& region = m_clickRegions[i];
            if (isPointInRect(cursor.x, cursor.y, region.x, region.y, region.width, region.height)) {
                newHoveredIndex = static_cast<int>(i);
                break;
            }
        }
        if (newHoveredIndex != m_hoveredRegionIndex) {
            m_hoveredRegionIndex = newHoveredIndex;
            rebuildRenderData();  // Rebuild to update button backgrounds
        }

        // For hotkeys tab, track row and column hover
        if (m_activeTab == TAB_HOTKEYS && m_hotkeyRowHeight > 0.0f) {
            int newHoveredRow = -1;
            HotkeyColumn newHoveredColumn = HotkeyColumn::NONE;

            if (cursor.y >= m_hotkeyContentStartY) {
                // Calculate which row the mouse is over
                float relativeY = cursor.y - m_hotkeyContentStartY;

                // Row 0 is Settings Menu
                if (relativeY < m_hotkeyRowHeight) {
                    newHoveredRow = 0;
                } else {
                    // After row 0, there's a 0.5 row gap, then rows 1+
                    float afterFirstRow = relativeY - m_hotkeyRowHeight;
                    float gapHeight = m_hotkeyRowHeight * 0.5f;

                    if (afterFirstRow >= gapHeight) {
                        float afterGap = afterFirstRow - gapHeight;
                        newHoveredRow = 1 + static_cast<int>(afterGap / m_hotkeyRowHeight);
                    }
                    // During gap, newHoveredRow stays -1
                }

                // Check which column the cursor is in (only if on a valid row)
                if (newHoveredRow >= 0) {
                    constexpr int kbFieldWidth = 16;
                    constexpr int ctrlFieldWidth = 12;
                    float kbFieldEnd = m_hotkeyKeyboardX + m_hotkeyFieldCharWidth * (kbFieldWidth + 2);
                    float ctrlFieldEnd = m_hotkeyControllerX + m_hotkeyFieldCharWidth * (ctrlFieldWidth + 2);

                    if (cursor.x >= m_hotkeyKeyboardX && cursor.x < kbFieldEnd) {
                        newHoveredColumn = HotkeyColumn::KEYBOARD;
                    } else if (cursor.x >= m_hotkeyControllerX && cursor.x < ctrlFieldEnd) {
                        newHoveredColumn = HotkeyColumn::CONTROLLER;
                    }
                }
            }

            if (newHoveredRow != m_hoveredHotkeyRow || newHoveredColumn != m_hoveredHotkeyColumn) {
                m_hoveredHotkeyRow = newHoveredRow;
                m_hoveredHotkeyColumn = newHoveredColumn;
                rebuildRenderData();
            }
        }

        // For riders tab, track which tracked rider cell is hovered
        if (m_activeTab == TAB_RIDERS && m_trackedRidersCellHeight > 0.0f && m_trackedRidersPerRow > 0) {
            int newHoveredIndex = -1;

            if (cursor.y >= m_trackedRidersStartY && cursor.x >= m_trackedRidersStartX) {
                float relativeY = cursor.y - m_trackedRidersStartY;
                float relativeX = cursor.x - m_trackedRidersStartX;

                int row = static_cast<int>(relativeY / m_trackedRidersCellHeight);
                int col = static_cast<int>(relativeX / m_trackedRidersCellWidth);

                if (col >= 0 && col < m_trackedRidersPerRow) {
                    newHoveredIndex = row * m_trackedRidersPerRow + col;
                }
            }

            if (newHoveredIndex != m_hoveredTrackedRiderIndex) {
                m_hoveredTrackedRiderIndex = newHoveredIndex;
                rebuildRenderData();
            }
        }
    }

    // Handle mouse input
    if (input.getLeftButton().isClicked()) {
        if (cursor.isValid) {
            handleClick(cursor.x, cursor.y);
        }
    }

    // Handle right-click for shape cycling (TAB_RIDERS only)
    if (input.getRightButton().isClicked()) {
        if (cursor.isValid && m_activeTab == TAB_RIDERS) {
            handleRightClick(cursor.x, cursor.y);
        }
    }

    // Handle hotkey capture mode
    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    if (hotkeyMgr.isCapturing()) {
        // Check for ESC to cancel capture
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            hotkeyMgr.cancelCapture();
            rebuildRenderData();
        }
        // Rebuild every frame during capture to show real-time modifier feedback
        else {
            rebuildRenderData();
        }
    }
    // Check if capture completed (must be outside isCapturing block - capture ends same frame)
    if (hotkeyMgr.wasCaptureCompleted()) {
        rebuildRenderData();
        // Save settings after binding change
        SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());
    }

    // Check if layout dirty (e.g., scale changed)
    if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void SettingsHud::rebuildLayout() {
    // Rebuild everything for layout changes (dragging, scale, etc.)
    // Given the complexity of tabs and dynamic controls, full rebuild is simplest
    if (m_bVisible) {
        rebuildRenderData();
    }
}

void SettingsHud::addClickRegion(ClickRegion::Type type, float x, float y, float width, float height,
                                 BaseHud* targetHud, uint32_t* bitfield, uint8_t* displayMode,
                                 uint32_t flagBit, bool isRequired, int tabIndex) {
    // Helper to create and add a ClickRegion with less boilerplate
    ClickRegion region;
    region.x = x;
    region.y = y;
    region.width = width;
    region.height = height;
    region.type = type;
    region.targetHud = targetHud;
    region.flagBit = flagBit;
    region.isRequired = isRequired;
    region.tabIndex = tabIndex;

    // Set the appropriate variant member based on type
    if (type == ClickRegion::CHECKBOX && bitfield != nullptr) {
        region.targetPointer = bitfield;
    } else if ((type == ClickRegion::DISPLAY_MODE_UP || type == ClickRegion::DISPLAY_MODE_DOWN) && displayMode != nullptr) {
        region.targetPointer = displayMode;
    } else {
        region.targetPointer = std::monostate{};
    }

    m_clickRegions.push_back(region);
}

float SettingsHud::addDisplayModeControl(float x, float& currentY, const ScaledDimensions& dims,
                                         uint8_t* displayMode, BaseHud* targetHud) {
    // Renders "Display < Mode >" with cycle control
    // Returns the final Y position after rendering

    // Determine display mode text
    const char* displayModeText = "";
    if (*displayMode == 0) {
        displayModeText = "Graphs";  // Enum value 0
    } else if (*displayMode == 1) {
        displayModeText = "Numbers"; // Enum value 1
    } else if (*displayMode == 2) {
        displayModeText = "Both";    // Enum value 2
    }

    // Render label
    addString("Display", x, currentY, Justify::LEFT,
        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);

    // Cycle control position
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    float controlX = x + PluginUtils::calculateMonospaceTextWidth(12, dims.fontSize);  // Align with other controls
    constexpr int MAX_VALUE_WIDTH = 7;  // "Numbers" is longest

    // Left arrow "<"
    addString("<", controlX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dims.fontSize);
    addClickRegion(ClickRegion::DISPLAY_MODE_DOWN, controlX, currentY, charWidth * 2, dims.lineHeightNormal,
                   targetHud, nullptr, displayMode, 0, false, 0);
    controlX += charWidth * 2;

    // Value with fixed width
    char paddedValue[32];
    snprintf(paddedValue, sizeof(paddedValue), "%-*s", MAX_VALUE_WIDTH, displayModeText);
    addString(paddedValue, controlX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dims.fontSize);
    controlX += PluginUtils::calculateMonospaceTextWidth(MAX_VALUE_WIDTH, dims.fontSize);

    // Right arrow " >"
    addString(" >", controlX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dims.fontSize);
    addClickRegion(ClickRegion::DISPLAY_MODE_UP, controlX, currentY, charWidth * 2, dims.lineHeightNormal,
                   targetHud, nullptr, displayMode, 0, false, 0);

    // Advance Y position
    currentY += dims.lineHeightNormal;

    return currentY;
}

void SettingsHud::rebuildRenderData() {
    if (!m_bVisible) return;

    m_strings.clear();
    m_quads.clear();
    m_clickRegions.clear();

    // Update cached window size (use actual pixel dimensions)
    const InputManager& input = InputManager::getInstance();
    m_cachedWindowWidth = input.getWindowWidth();
    m_cachedWindowHeight = input.getWindowHeight();

    auto dim = getScaledDimensions();

    // Layout constants - compact panel for single HUD
    constexpr int panelWidthChars = SettingsHud::SETTINGS_PANEL_WIDTH;
    constexpr float sectionSpacing = 0.0150f;
    constexpr float tabSpacing = 0.0050f;

    float panelWidth = PluginUtils::calculateMonospaceTextWidth(panelWidthChars, dim.fontSize) + dim.paddingH + dim.paddingH;

    // Estimate height - sized to fit Riders tab content (6 server + 12 tracked + headers + pagination)
    int estimatedRows = 25;
    float backgroundHeight = dim.paddingV + dim.lineHeightLarge + dim.lineHeightNormal + (estimatedRows * dim.lineHeightNormal) + dim.paddingV;

    // Center the panel horizontally and vertically
    float startX = (1.0f - panelWidth) / 2.0f;
    float startY = (1.0f - backgroundHeight) / 2.0f;

    setBounds(startX, startY, startX + panelWidth, startY + backgroundHeight);
    addBackgroundQuad(startX, startY, panelWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float currentY = startY + dim.paddingV;

    // Main title
    float titleX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
    addString("MXBMRP3 SETTINGS", titleX, currentY, Justify::CENTER,
        Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);

    currentY += dim.lineHeightLarge + tabSpacing;

    // Vertical tab bar on left side
    float tabStartX = contentStartX;
    float tabStartY = currentY;
    float tabWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_TAB_WIDTH, dim.fontSize);

    float checkboxWidth = PluginUtils::calculateMonospaceTextWidth(4, dim.fontSize);  // "[X] " or "    "

    // Define visual tab order with section markers
    static constexpr int TAB_SECTION_GLOBAL = -1;
    static constexpr int TAB_SECTION_PROFILE = -2;
    static constexpr int TAB_SECTION_ELEMENTS = -3;
    static constexpr int tabDisplayOrder[] = {
        TAB_SECTION_GLOBAL,
        TAB_GENERAL,
        TAB_APPEARANCE,
        TAB_HOTKEYS,
        TAB_RIDERS,
        TAB_RUMBLE,
        TAB_SECTION_PROFILE,
        TAB_SECTION_ELEMENTS,
        TAB_STANDINGS,
        TAB_MAP,
        TAB_RADAR,
        TAB_LAP_LOG,
        TAB_IDEAL_LAP,
        TAB_TELEMETRY,
        TAB_INPUT,
        TAB_RECORDS,
        TAB_PITBOARD,
        TAB_TIMING,
        TAB_GAP_BAR,
        TAB_PERFORMANCE,
        TAB_WIDGETS
    };

    for (size_t orderIdx = 0; orderIdx < sizeof(tabDisplayOrder)/sizeof(tabDisplayOrder[0]); orderIdx++) {
        int i = tabDisplayOrder[orderIdx];

        // Section headers (bold, primary color, not clickable)
        if (i == TAB_SECTION_GLOBAL) {
            addString("Global", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }
        if (i == TAB_SECTION_PROFILE) {
            tabStartY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
            addString("Profile", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;

            // Profile cycle control: < Practice >
            float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
            const char* profileName = ProfileManager::getProfileName(activeProfile);

            float currentX = tabStartX;

            // Left arrow "<" with click region (cycles to previous profile)
            addString("<", currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, tabStartY, charWidth * 2, dim.lineHeightNormal,
                ClickRegion::PROFILE_CYCLE_DOWN, nullptr
            ));
            currentX += charWidth * 2;

            // Profile name (not clickable)
            char profileLabel[12];
            snprintf(profileLabel, sizeof(profileLabel), "%-8s", profileName);
            addString(profileLabel, currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentX += charWidth * 8;

            // Right arrow " >" with click region (cycles to next profile)
            addString(" >", currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, tabStartY, charWidth * 2, dim.lineHeightNormal,
                ClickRegion::PROFILE_CYCLE_UP, nullptr
            ));

            tabStartY += dim.lineHeightNormal;
            continue;
        }
        if (i == TAB_SECTION_ELEMENTS) {
            tabStartY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
            addString("Elements", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }

        bool isActive = (i == m_activeTab);

        // Get the HUD for this tab (nullptr for General and Widgets)
        BaseHud* tabHud = nullptr;
        switch (i) {
            case TAB_STANDINGS:    tabHud = m_standings; break;
            case TAB_MAP:          tabHud = m_mapHud; break;
            case TAB_PITBOARD:     tabHud = m_pitboard; break;
            case TAB_LAP_LOG:      tabHud = m_lapLog; break;
            case TAB_IDEAL_LAP: tabHud = m_idealLap; break;
            case TAB_TELEMETRY:    tabHud = m_telemetry; break;
            case TAB_INPUT:        tabHud = m_input; break;
            case TAB_PERFORMANCE:  tabHud = m_performance; break;
            case TAB_RECORDS:      tabHud = m_records; break;
            case TAB_RADAR:        tabHud = m_radarHud; break;
            case TAB_TIMING:       tabHud = m_timing; break;
            case TAB_GAP_BAR:      tabHud = m_gapBar; break;
            default:               tabHud = nullptr; break;  // General, Widgets have no single HUD
        }

        // Determine if this tab's HUD/widgets are enabled
        bool isHudEnabled;
        if (tabHud) {
            isHudEnabled = tabHud->isVisible();
        } else if (i == TAB_WIDGETS) {
            isHudEnabled = HudManager::getInstance().areWidgetsEnabled();
        } else if (i == TAB_RUMBLE) {
            isHudEnabled = XInputReader::getInstance().getRumbleConfig().enabled;
        } else {
            isHudEnabled = true;  // General is always "enabled"
        }

        // Tab color: PRIMARY if active, ACCENT if inactive
        unsigned long tabColor = isActive ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getAccent();

        float currentTabX = tabStartX;

        // Add checkbox for tabs with toggleable HUDs or widgets
        if (tabHud) {
            // Checkbox click region for individual HUD
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HUD_TOGGLE, tabHud
            ));

            // Checkbox text
            const char* checkboxText = isHudEnabled ? "[X]" : "[ ]";
            addString(checkboxText, currentTabX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

            currentTabX += checkboxWidth;
        } else if (i == TAB_WIDGETS) {
            // Checkbox click region for widgets master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::WIDGETS_TOGGLE, nullptr
            ));

            // Checkbox text
            const char* checkboxText = isHudEnabled ? "[X]" : "[ ]";
            addString(checkboxText, currentTabX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

            currentTabX += checkboxWidth;
        } else if (i == TAB_RUMBLE) {
            // Checkbox click region for rumble master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::RUMBLE_TOGGLE, nullptr
            ));

            // Checkbox text
            const char* checkboxText = isHudEnabled ? "[X]" : "[ ]";
            addString(checkboxText, currentTabX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

            currentTabX += checkboxWidth;
        } else {
            // No checkbox for General - just add spacing
            currentTabX += checkboxWidth;
        }

        // Tab click region (for selecting the tab)
        float tabLabelWidth = tabWidth - checkboxWidth;
        size_t tabRegionIndex = m_clickRegions.size();  // Track index for hover check
        ClickRegion tabRegion;
        tabRegion.x = currentTabX;
        tabRegion.y = tabStartY;
        tabRegion.width = tabLabelWidth;
        tabRegion.height = dim.lineHeightNormal;
        tabRegion.type = ClickRegion::TAB;
        tabRegion.targetPointer = std::monostate{};
        tabRegion.flagBit = 0;
        tabRegion.isRequired = false;
        tabRegion.targetHud = nullptr;
        tabRegion.tabIndex = i;
        m_clickRegions.push_back(tabRegion);

        // Active tab background
        if (isActive) {
            SPluginQuad_t bgQuad;
            float bgX = currentTabX, bgY = tabStartY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, tabLabelWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        // Hover background for inactive tabs
        else if (m_hoveredRegionIndex >= 0 && static_cast<size_t>(m_hoveredRegionIndex) == tabRegionIndex) {
            SPluginQuad_t hoverQuad;
            float hoverX = currentTabX, hoverY = tabStartY;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverQuad, hoverX, hoverY, tabLabelWidth, dim.lineHeightNormal);
            hoverQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            hoverQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverQuad);
        }

        const char* tabName = i == TAB_GENERAL ? "General" :
                              i == TAB_APPEARANCE ? "Appearance" :
                              i == TAB_STANDINGS ? "Standings" :
                              i == TAB_MAP ? "Map" :
                              i == TAB_LAP_LOG ? "Lap Log" :
                              i == TAB_IDEAL_LAP ? "Ideal Lap" :
                              i == TAB_TELEMETRY ? "Telemetry" :
                              i == TAB_INPUT ? "Input" :
                              i == TAB_PERFORMANCE ? "Performance" :
                              i == TAB_PITBOARD ? "Pitboard" :
                              i == TAB_RECORDS ? "Records" :
                              i == TAB_TIMING ? "Timing" :
                              i == TAB_GAP_BAR ? "Gap Bar" :
                              i == TAB_WIDGETS ? "Widgets" :
                              i == TAB_RUMBLE ? "Rumble" :
                              i == TAB_HOTKEYS ? "Hotkeys" :
                              i == TAB_RIDERS ? "Riders" :
                              "Radar";

        addString(tabName, currentTabX, tabStartY, Justify::LEFT, Fonts::getNormal(), tabColor, dim.fontSize);

        tabStartY += dim.lineHeightNormal;
    }

    // Content area starts to the right of the tabs
    float contentAreaStartX = contentStartX + tabWidth + PluginUtils::calculateMonospaceTextWidth(2, dim.fontSize);  // 2-char gap after tabs
    currentY = currentY;  // Content starts at same Y as tabs

    // Helper lambdas for controls
    // NOTE: These lambdas are intentionally NOT extracted to member functions.
    // They capture local layout state (dim, currentY, contentAreaStartX, etc.) which
    // would require passing 8+ parameters if converted to methods. Lambdas improve
    // readability and maintainability here. See CLAUDE.md "Design Decisions".
    float leftColumnX = contentAreaStartX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_LEFT_COLUMN, dim.fontSize);
    float rightColumnX = contentAreaStartX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_RIGHT_COLUMN, dim.fontSize);

    // Helper lambda to add cycle control with < value > pattern - shared across all controls
    // If enabled is false, no click regions are added and muted color is used
    // If isOff is true, the value is muted (for "Off" state visual consistency)
    auto addCycleControl = [&](float baseX, float y, const char* value, int maxValueWidth,
                               ClickRegion::Type downType, ClickRegion::Type upType, BaseHud* targetHud,
                               bool enabled = true, bool isOff = false) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long valueColor = (enabled && !isOff) ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();

        // Left arrow "<" - only show when enabled
        if (enabled) {
            addString("<", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                downType, targetHud, 0, false, 0
            ));
        }
        currentX += charWidth * 2;  // "< " (spacing preserved even if arrow hidden)

        // Value with fixed width (left-aligned, padded)
        char paddedValue[32];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", maxValueWidth, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(maxValueWidth, dim.fontSize);

        // Right arrow " >" - only show when enabled
        if (enabled) {
            addString(" >", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                upType, targetHud, 0, false, 0
            ));
        }
    };

    // Helper lambda to add toggle control with < On/Off > pattern - for boolean settings
    // Both arrows trigger the same toggle action. If enabled is false, muted colors are used.
    // "Off" values are also muted for visual consistency.
    auto addToggleControl = [&](float baseX, float y, bool isOn,
                                ClickRegion::Type toggleType, BaseHud* targetHud,
                                uint32_t* bitfield = nullptr, uint32_t flag = 0,
                                bool enabled = true) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long valueColor = (enabled && isOn) ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();
        const char* value = isOn ? "On" : "Off";
        constexpr int VALUE_WIDTH = 3;  // "Off" is longest

        // Left arrow "<" - only show when enabled
        if (enabled) {
            addString("<", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            if (bitfield != nullptr) {
                // CHECKBOX type with bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, bitfield, flag, false, targetHud
                ));
            } else {
                // Simple toggle without bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, targetHud
                ));
            }
        }
        currentX += charWidth * 2;  // "< " (spacing preserved even if arrow hidden)

        // Value with fixed width
        char paddedValue[8];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", VALUE_WIDTH, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, dim.fontSize);

        // Right arrow " >" - only show when enabled
        if (enabled) {
            addString(" >", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            if (bitfield != nullptr) {
                // CHECKBOX type with bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, bitfield, flag, false, targetHud
                ));
            } else {
                // Simple toggle without bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, targetHud
                ));
            }
        }
    };

    auto addHudControls = [&](BaseHud* hud, bool enableTitle = true) -> float {
        // Save starting Y for right column (data toggles)
        float sectionStartY = currentY;

        // LEFT COLUMN: Basic controls
        float controlX = leftColumnX;
        float toggleX = controlX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);  // Align all toggles

        // Visibility toggle
        bool isVisible = hud->isVisible();
        addString("Visible", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        addToggleControl(toggleX, currentY, isVisible, ClickRegion::HUD_TOGGLE, hud);
        currentY += dim.lineHeightNormal;

        // Title toggle (can be disabled/grayed out)
        bool showTitle = enableTitle ? hud->getShowTitle() : false;
        addString("Title", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), enableTitle ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        addToggleControl(toggleX, currentY, showTitle, ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, enableTitle);
        currentY += dim.lineHeightNormal;

        // Background texture variant cycle (Off, 1, 2, ...)
        // Only enable if textures are available for this HUD
        bool hasTextures = !hud->getAvailableTextureVariants().empty();
        addString("Texture", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), hasTextures ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        char textureValue[16];
        int variant = hud->getTextureVariant();
        if (!hasTextures || variant == 0) {
            snprintf(textureValue, sizeof(textureValue), "Off");
        } else {
            snprintf(textureValue, sizeof(textureValue), "%d", variant);
        }
        addCycleControl(toggleX, currentY, textureValue, 4,
            ClickRegion::TEXTURE_VARIANT_DOWN, ClickRegion::TEXTURE_VARIANT_UP, hud, hasTextures);
        currentY += dim.lineHeightNormal;

        // Background opacity controls
        addString("Opacity", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char opacityValue[16];
        int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
        snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
        addCycleControl(toggleX, currentY, opacityValue, 4,
            ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud);
        currentY += dim.lineHeightNormal;

        // Scale controls
        addString("Scale", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char scaleValue[16];
        int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
        snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
        addCycleControl(toggleX, currentY, scaleValue, 4,
            ClickRegion::SCALE_DOWN, ClickRegion::SCALE_UP, hud);
        currentY += dim.lineHeightNormal;

        // Return the starting Y for right column (data toggles)
        return sectionStartY;
    };

    // Widget table row - displays widget settings in columnar format
    // Layout: Name | Visible | Title | BG Tex | Opacity | Scale
    auto addWidgetRow = [&](const char* name, BaseHud* hud, bool enableTitle = true, bool enableOpacity = true, bool enableScale = true, bool enableVisibility = true, bool enableBgTexture = true) {
        // Column positions (spacing for table layout with toggle controls)
        float nameX = leftColumnX;
        float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);   // After name
        float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize);   // After Vis toggle (< On >)
        float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // After Title toggle
        float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // After BG Tex toggle
        float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize); // After Opacity cycle

        // Widget name
        addString(name, nameX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);

        // Visibility toggle (shows actual value, grayed out when disabled)
        addToggleControl(visX, currentY, hud->isVisible(), ClickRegion::HUD_TOGGLE, hud, nullptr, 0, enableVisibility);

        // Title toggle (shows actual value, grayed out when disabled)
        addToggleControl(titleX, currentY, hud->getShowTitle(), ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, enableTitle);

        // BG Texture variant cycle (disabled if no textures available)
        bool hasTextures = !hud->getAvailableTextureVariants().empty();
        char texValue[8];
        int texVariant = hud->getTextureVariant();
        snprintf(texValue, sizeof(texValue), (!hasTextures || texVariant == 0) ? "Off" : "%d", texVariant);
        addCycleControl(bgTexX, currentY, texValue, 3,
            ClickRegion::TEXTURE_VARIANT_DOWN, ClickRegion::TEXTURE_VARIANT_UP, hud, enableBgTexture && hasTextures);

        // BG Opacity (shows muted value without arrows when disabled)
        char opacityValue[16];
        int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
        snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
        addCycleControl(opacityX, currentY, opacityValue, 4,
            ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud, enableOpacity);

        // Scale (shows muted value without arrows when disabled)
        char scaleValue[16];
        int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
        snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
        addCycleControl(scaleX, currentY, scaleValue, 4,
            ClickRegion::SCALE_DOWN, ClickRegion::SCALE_UP, hud, enableScale);

        currentY += dim.lineHeightNormal;
    };

    // Data toggle control - displays "Label: < On/Off >" format
    // labelWidth should accommodate the longest label in the group for alignment
    auto addDataToggle = [&](const char* label, uint32_t* bitfield, uint32_t flag, bool isRequired, BaseHud* hud, float yPos, int labelWidth = 12) {
        float dataX = rightColumnX;
        bool isChecked = (*bitfield & flag) != 0;
        bool enabled = !isRequired;

        // Label with padding
        char paddedLabel[32];
        snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::getNormal(),
            enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);

        // Toggle control
        float toggleX = dataX + PluginUtils::calculateMonospaceTextWidth(labelWidth, dim.fontSize);
        addToggleControl(toggleX, yPos, isChecked, ClickRegion::CHECKBOX, hud, bitfield, flag, enabled);
    };

    // Helper for grouped toggles that toggle multiple bits
    auto addGroupToggle = [&](const char* label, uint32_t* bitfield, uint32_t groupFlags, bool isRequired, BaseHud* hud, float yPos, int labelWidth = 12) {
        float dataX = rightColumnX;
        // Group is checked if all bits in group are set
        bool isChecked = (*bitfield & groupFlags) == groupFlags;
        bool enabled = !isRequired;

        // Label with padding
        char paddedLabel[32];
        snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::getNormal(),
            enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);

        // Toggle control
        float toggleX = dataX + PluginUtils::calculateMonospaceTextWidth(labelWidth, dim.fontSize);
        addToggleControl(toggleX, yPos, isChecked, ClickRegion::CHECKBOX, hud, bitfield, groupFlags, enabled);
    };

    // Render controls for active tab only
    BaseHud* activeHud = nullptr;
    float dataStartY = 0.0f;

    switch (m_activeTab) {
        case TAB_GENERAL:
        {
            // General settings tab
            ColorConfig& colorConfig = ColorConfig::getInstance();

            // Preferences section (first - no spacing before)
            addString("Preferences", leftColumnX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal;

            // Controller selector (used by both Input HUD and Rumble)
            // Cycles: Disabled -> 1 -> 2 -> 3 -> 4 -> Disabled
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
                int controllerIdx = rumbleConfig.controllerIndex;
                bool isDisabled = (controllerIdx < 0);
                bool isConnected = !isDisabled && XInputReader::isControllerConnected(controllerIdx);
                std::string controllerName = isDisabled ? "" : XInputReader::getControllerName(controllerIdx);

                addString("Controller", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                float currentX = toggleX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                currentX += charWidth * 2;

                // Show controller status
                // Format: "Disabled" or "1: Name..." or "1: Not Connected"
                char displayStr[40];
                if (isDisabled) {
                    snprintf(displayStr, sizeof(displayStr), "Disabled");
                } else {
                    int slot = controllerIdx + 1;
                    if (!controllerName.empty()) {
                        snprintf(displayStr, sizeof(displayStr), "%d: %-20.20s", slot, controllerName.c_str());
                    } else if (isConnected) {
                        snprintf(displayStr, sizeof(displayStr), "%d: Connected", slot);
                    } else {
                        snprintf(displayStr, sizeof(displayStr), "%d: Not Connected", slot);
                    }
                }

                // Color: muted for disabled, positive for connected, muted for not connected
                uint32_t textColor = isDisabled ? ColorConfig::getInstance().getMuted() :
                    (isConnected ? ColorConfig::getInstance().getPositive() : ColorConfig::getInstance().getMuted());
                addString(displayStr, currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), textColor, dim.fontSize);
                currentX += charWidth * 24;

                addString(">", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);

                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 27, dim.lineHeightNormal,
                    ClickRegion::RUMBLE_CONTROLLER_UP, nullptr
                ));

                currentY += dim.lineHeightNormal;
            }

            // Speed unit toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                addString("Speed", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current unit with < > cycle pattern (arrows=accent, value=primary)
                bool isKmh = m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
                float currentX = toggleX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                currentX += charWidth * 2;
                addString(isKmh ? "km/h" : "mph ", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                currentX += charWidth * 4;
                addString(" >", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);

                // Click region for the entire toggle area
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 8, dim.lineHeightNormal,
                    ClickRegion::SPEED_UNIT_TOGGLE, m_speed
                ));

                currentY += dim.lineHeightNormal;
            }

            // Fuel unit toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                addString("Fuel", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current unit with < > cycle pattern (arrows=accent, value=primary)
                bool isGallons = m_fuel && m_fuel->getFuelUnit() == FuelWidget::FuelUnit::GALLONS;
                float currentX = toggleX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                currentX += charWidth * 2;
                addString(isGallons ? "gal" : "L  ", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                currentX += charWidth * 3;
                addString(" >", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);

                // Click region for the entire toggle area
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 8, dim.lineHeightNormal,
                    ClickRegion::FUEL_UNIT_TOGGLE, m_fuel
                ));

                currentY += dim.lineHeightNormal;
            }

            // Grid snap toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                addString("Grid Snap", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current state with < > cycle pattern (arrows=accent, value=primary)
                bool gridSnapEnabled = ColorConfig::getInstance().getGridSnapping();
                float currentX = toggleX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                currentX += charWidth * 2;
                addString(gridSnapEnabled ? "On " : "Off", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), gridSnapEnabled ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
                currentX += charWidth * 3;
                addString(" >", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);

                // Click region for the entire toggle area
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 7, dim.lineHeightNormal,
                    ClickRegion::GRID_SNAP_TOGGLE, nullptr
                ));

                currentY += dim.lineHeightNormal;
            }

            // Check for Updates toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                addString("Updates", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current state with < > cycle pattern
                bool updatesEnabled = UpdateChecker::getInstance().isEnabled();
                float currentX = toggleX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                currentX += charWidth * 2;
                addString(updatesEnabled ? "On " : "Off", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), updatesEnabled ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
                currentX += charWidth * 3;
                addString(" >", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);

                // Click region for the entire toggle area
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 7, dim.lineHeightNormal,
                    ClickRegion::UPDATE_CHECK_TOGGLE, nullptr
                ));

                currentY += dim.lineHeightNormal;
            }

            // Profiles section
            currentY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
            addString("Profiles", leftColumnX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal;

            // Auto-switch toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                addString("Auto-Switch", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current state with < > cycle pattern (arrows=accent, value=primary)
                bool autoSwitchEnabled = ProfileManager::getInstance().isAutoSwitchEnabled();
                float currentX = toggleX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                currentX += charWidth * 2;
                addString(autoSwitchEnabled ? "On " : "Off", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), autoSwitchEnabled ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
                currentX += charWidth * 3;
                addString(" >", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);

                // Click region for the entire toggle area
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 7, dim.lineHeightNormal,
                    ClickRegion::AUTO_SWITCH_TOGGLE, nullptr
                ));

                currentY += dim.lineHeightNormal;
            }

            // Copy profile: "Copy [Profile] profile to < target >" with [Copy] button
            currentY += dim.lineHeightNormal * 0.5f;  // Extra spacing before copy
            {
                ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
                const char* activeProfileName = ProfileManager::getInstance().getProfileName(activeProfile);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                // Build the display: "Copy Practice profile to < All >"
                float currentX = leftColumnX;

                addString("Copy", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                currentX += charWidth * 5;

                addString(activeProfileName, currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                currentX += charWidth * 9;

                addString("profile to", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                currentX += charWidth * 11;

                // Target cycle control
                const char* targetName;
                bool hasTarget = (m_copyTargetProfile != -1);
                if (m_copyTargetProfile == -1) {
                    targetName = "Select";
                } else if (m_copyTargetProfile == 4) {
                    targetName = "All";
                } else {
                    targetName = ProfileManager::getInstance().getProfileName(static_cast<ProfileType>(m_copyTargetProfile));
                }

                // Left arrow "<" with click region
                float arrowStartX = currentX;
                addString("<", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::COPY_TARGET_DOWN, nullptr
                ));
                currentX += charWidth * 2;

                // Target name (no click region)
                unsigned long targetColor = hasTarget ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();
                addString(targetName, currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), targetColor, dim.fontSize);
                currentX += charWidth * 8;

                // Right arrow ">" with click region
                addString(" >", currentX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::COPY_TARGET_UP, nullptr
                ));

                currentY += dim.lineHeightNormal;

                // [Copy] button - centered like [Close] button
                currentY += dim.lineHeightNormal * 0.5f;
                {
                    float buttonWidth = PluginUtils::calculateMonospaceTextWidth(6, dim.fontSize);
                    float buttonCenterX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
                    float buttonX = buttonCenterX - buttonWidth / 2.0f;

                    size_t regionIndex = m_clickRegions.size();
                    m_clickRegions.push_back(ClickRegion(
                        buttonX, currentY, buttonWidth, dim.lineHeightNormal,
                        ClickRegion::COPY_BUTTON, nullptr
                    ));

                    if (hasTarget) {
                        SPluginQuad_t bgQuad;
                        float bgX = buttonX, bgY = currentY;
                        applyOffset(bgX, bgY);
                        setQuadPositions(bgQuad, bgX, bgY, buttonWidth, dim.lineHeightNormal);
                        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(regionIndex))
                            ? ColorConfig::getInstance().getAccent()
                            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
                        m_quads.push_back(bgQuad);
                    }

                    unsigned long textColor = !hasTarget ? ColorConfig::getInstance().getMuted()
                        : (m_hoveredRegionIndex == static_cast<int>(regionIndex))
                            ? ColorConfig::getInstance().getPrimary()
                            : ColorConfig::getInstance().getSecondary();
                    addString("[Copy]", buttonCenterX, currentY, Justify::CENTER,
                        Fonts::getNormal(), textColor, dim.fontSize);

                    currentY += dim.lineHeightNormal;
                }
            }

            // Reset section - radio options + [Reset] button
            currentY += dim.lineHeightNormal * 0.5f;
            {
                ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
                const char* activeProfileName = ProfileManager::getInstance().getProfileName(activeProfile);
                float radioWidth = PluginUtils::calculateMonospaceTextWidth(CHECKBOX_WIDTH, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                // Reset [Profile] profile radio row
                {
                    float rowWidth = radioWidth + PluginUtils::calculateMonospaceTextWidth(22, dim.fontSize);
                    m_clickRegions.push_back(ClickRegion(
                        leftColumnX, currentY, rowWidth, dim.lineHeightNormal,
                        ClickRegion::RESET_PROFILE_CHECKBOX, nullptr
                    ));

                    addString(m_resetProfileConfirmed ? "(O)" : "( )", leftColumnX, currentY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                    float textX = leftColumnX + radioWidth;
                    unsigned long labelColor = ColorConfig::getInstance().getSecondary();
                    unsigned long profileColor = m_resetProfileConfirmed
                        ? ColorConfig::getInstance().getPrimary()
                        : ColorConfig::getInstance().getSecondary();

                    addString("Reset", textX, currentY, Justify::LEFT,
                        Fonts::getNormal(), labelColor, dim.fontSize);
                    textX += charWidth * 6;

                    addString(activeProfileName, textX, currentY, Justify::LEFT,
                        Fonts::getNormal(), profileColor, dim.fontSize);
                    textX += charWidth * 9;

                    addString("profile", textX, currentY, Justify::LEFT,
                        Fonts::getNormal(), labelColor, dim.fontSize);

                    currentY += dim.lineHeightNormal;
                }

                // Reset All Settings radio row
                {
                    float rowWidth = radioWidth + PluginUtils::calculateMonospaceTextWidth(18, dim.fontSize);
                    m_clickRegions.push_back(ClickRegion(
                        leftColumnX, currentY, rowWidth, dim.lineHeightNormal,
                        ClickRegion::RESET_ALL_CHECKBOX, nullptr
                    ));

                    addString(m_resetAllConfirmed ? "(O)" : "( )", leftColumnX, currentY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                    unsigned long labelColor = m_resetAllConfirmed
                        ? ColorConfig::getInstance().getPrimary()
                        : ColorConfig::getInstance().getSecondary();
                    addString("Reset All Settings", leftColumnX + radioWidth, currentY, Justify::LEFT,
                        Fonts::getNormal(), labelColor, dim.fontSize);

                    currentY += dim.lineHeightNormal;
                }

                // [Reset] button - centered like [Close] button
                currentY += dim.lineHeightNormal * 0.5f;
                {
                    bool resetEnabled = m_resetProfileConfirmed || m_resetAllConfirmed;
                    float buttonWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
                    float buttonCenterX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
                    float buttonX = buttonCenterX - buttonWidth / 2.0f;

                    size_t regionIndex = m_clickRegions.size();
                    m_clickRegions.push_back(ClickRegion(
                        buttonX, currentY, buttonWidth, dim.lineHeightNormal,
                        ClickRegion::RESET_BUTTON, nullptr
                    ));

                    if (resetEnabled) {
                        SPluginQuad_t bgQuad;
                        float bgX = buttonX, bgY = currentY;
                        applyOffset(bgX, bgY);
                        setQuadPositions(bgQuad, bgX, bgY, buttonWidth, dim.lineHeightNormal);
                        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(regionIndex))
                            ? ColorConfig::getInstance().getAccent()
                            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
                        m_quads.push_back(bgQuad);
                    }

                    unsigned long textColor = !resetEnabled ? ColorConfig::getInstance().getMuted()
                        : (m_hoveredRegionIndex == static_cast<int>(regionIndex))
                            ? ColorConfig::getInstance().getPrimary()
                            : ColorConfig::getInstance().getSecondary();
                    addString("[Reset]", buttonCenterX, currentY, Justify::CENTER,
                        Fonts::getNormal(), textColor, dim.fontSize);

                    currentY += dim.lineHeightNormal;
                }
            }
        }
        break;

        case TAB_APPEARANCE:
        {
            // Appearance tab - font categories and color configuration
            FontConfig& fontConfig = FontConfig::getInstance();
            ColorConfig& colorConfig = ColorConfig::getInstance();
            float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

            // ============================================
            // Fonts section
            // ============================================
            addString("Fonts", leftColumnX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal;

            // Helper lambda to add a font category row with cycle buttons
            auto addFontRow = [&](FontCategory category) {
                const char* categoryName = FontConfig::getCategoryName(category);
                const char* fontDisplayName = fontConfig.getFontDisplayName(category);

                // Category name label
                addString(categoryName, leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Font name with cycle arrows
                float cycleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Left arrow "<" with click region for PREV
                addString("<", cycleX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    cycleX, currentY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::FONT_CATEGORY_PREV, category
                ));
                cycleX += charWidth * 2;

                // Font name (no click region)
                addString(fontDisplayName, cycleX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                cycleX += charWidth * 22;  // Max font display name width

                // Right arrow ">" with click region for NEXT
                addString(" >", cycleX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    cycleX, currentY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::FONT_CATEGORY_NEXT, category
                ));

                currentY += dim.lineHeightNormal;
            };

            // All font categories
            addFontRow(FontCategory::TITLE);
            addFontRow(FontCategory::NORMAL);
            addFontRow(FontCategory::STRONG);
            addFontRow(FontCategory::MARKER);
            addFontRow(FontCategory::SMALL);

            currentY += dim.lineHeightNormal * 0.5f;  // Spacing before Colors section

            // ============================================
            // Colors section
            // ============================================
            addString("Colors", leftColumnX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal;

            // Helper lambda to add a color row with preview and cycle buttons
            auto addColorRow = [&](ColorSlot slot) {
                const char* slotName = ColorConfig::getSlotName(slot);
                unsigned long color = colorConfig.getColor(slot);
                const char* colorName = ColorPalette::getColorName(color);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                // Slot name label
                addString(slotName, leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Color preview quad (small square showing the actual color)
                float previewX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                float previewSize = dim.lineHeightNormal * 0.8f;
                {
                    SPluginQuad_t previewQuad;
                    float quadX = previewX;
                    float quadY = currentY + dim.lineHeightNormal * 0.1f;
                    applyOffset(quadX, quadY);
                    setQuadPositions(previewQuad, quadX, quadY, previewSize, previewSize);
                    previewQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                    previewQuad.m_ulColor = color;
                    m_quads.push_back(previewQuad);
                }

                // Color name with cycle arrows (following addCycleControl pattern)
                float cycleX = previewX + previewSize + PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                // Left arrow "<" with click region for PREV
                addString("<", cycleX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    cycleX, currentY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::COLOR_CYCLE_PREV, slot
                ));
                cycleX += charWidth * 2;

                // Color name (no click region)
                addString(colorName, cycleX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                cycleX += charWidth * 10;  // Max color name width

                // Right arrow ">" with click region for NEXT
                addString(" >", cycleX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    cycleX, currentY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::COLOR_CYCLE_NEXT, slot
                ));

                currentY += dim.lineHeightNormal;
            };

            // All color slots
            addColorRow(ColorSlot::PRIMARY);
            addColorRow(ColorSlot::SECONDARY);
            addColorRow(ColorSlot::TERTIARY);
            addColorRow(ColorSlot::MUTED);
            addColorRow(ColorSlot::BACKGROUND);
            addColorRow(ColorSlot::ACCENT);
            addColorRow(ColorSlot::POSITIVE);
            addColorRow(ColorSlot::NEUTRAL);
            addColorRow(ColorSlot::WARNING);
            addColorRow(ColorSlot::NEGATIVE);
        }
        break;

        case TAB_HOTKEYS:
        {
            // Hotkeys tab - keyboard and controller binding configuration
            HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
            float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

            // Column layout - wider fields for better readability
            float actionX = leftColumnX;
            float keyboardX = actionX + charWidth * 14;  // After action name
            float controllerX = keyboardX + charWidth * 22;  // After keyboard binding (wider)

            // Field widths (characters inside brackets)
            constexpr int kbFieldWidth = 16;   // Fits "Ctrl+Shift+F12"
            constexpr int ctrlFieldWidth = 12; // Fits "Right Shoulder"

            addString("Toggle", actionX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            addString("Keyboard", keyboardX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            addString("Controller", controllerX, currentY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal;

            // Store layout info for hover detection in update()
            m_hotkeyContentStartY = currentY;
            m_hotkeyRowHeight = dim.lineHeightNormal;
            m_hotkeyKeyboardX = keyboardX;
            m_hotkeyControllerX = controllerX;
            m_hotkeyFieldCharWidth = charWidth;

            // Check if we're in capture mode
            bool isCapturing = hotkeyMgr.isCapturing();
            HotkeyAction captureAction = hotkeyMgr.getCaptureAction();
            CaptureType captureType = hotkeyMgr.getCaptureType();

            // Track row index for hover detection
            int currentRowIndex = 0;

            // Helper to add a hotkey row
            auto addHotkeyRow = [&](HotkeyAction action) {
                const HotkeyBinding& binding = hotkeyMgr.getBinding(action);

                // Check if this row is hovered (using tracked row index)
                bool isRowHovered = (currentRowIndex == m_hoveredHotkeyRow);

                // Action name
                addString(getActionDisplayName(action), actionX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Keyboard binding
                bool isCapturingKeyboard = isCapturing && captureAction == action && captureType == CaptureType::KEYBOARD;
                float kbX = keyboardX;

                if (isCapturingKeyboard) {
                    // Show capture prompt with real-time modifier feedback (accent color)
                    ModifierFlags currentMods = hotkeyMgr.getCurrentModifiers();
                    char capturePrompt[40];
                    std::string modPrefix;
                    if (hasModifier(currentMods, ModifierFlags::CTRL)) modPrefix += "Ctrl+";
                    if (hasModifier(currentMods, ModifierFlags::SHIFT)) modPrefix += "Shift+";
                    if (hasModifier(currentMods, ModifierFlags::ALT)) modPrefix += "Alt+";

                    if (modPrefix.empty()) {
                        snprintf(capturePrompt, sizeof(capturePrompt), "[%-*s]", kbFieldWidth, "Press Key...");
                    } else {
                        char inner[32];
                        snprintf(inner, sizeof(inner), "%s...", modPrefix.c_str());
                        snprintf(capturePrompt, sizeof(capturePrompt), "[%-*s]", kbFieldWidth, inner);
                    }
                    addString(capturePrompt, kbX, currentY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                } else {
                    // Show current binding with brackets
                    char keyStr[32];
                    formatKeyBinding(binding.keyboard, keyStr, sizeof(keyStr));

                    // Format as clickable: [binding] - wider field, truncate if too long
                    char displayStr[48];
                    snprintf(displayStr, sizeof(displayStr), "[%-*.*s]", kbFieldWidth, kbFieldWidth, keyStr);

                    // Determine color: hovered > bound > unbound
                    bool isKbHovered = (m_hoveredHotkeyRow == currentRowIndex && m_hoveredHotkeyColumn == HotkeyColumn::KEYBOARD);
                    unsigned long keyColor;
                    if (isKbHovered) {
                        keyColor = ColorConfig::getInstance().getAccent();
                    } else if (binding.hasKeyboard()) {
                        keyColor = ColorConfig::getInstance().getPrimary();
                    } else {
                        keyColor = ColorConfig::getInstance().getMuted();
                    }
                    addString(displayStr, kbX, currentY, Justify::LEFT,
                        Fonts::getNormal(), keyColor, dim.fontSize);

                    // Click region for keyboard binding (covers full field)
                    m_clickRegions.push_back(ClickRegion(
                        kbX, currentY, charWidth * (kbFieldWidth + 2), dim.lineHeightNormal,
                        ClickRegion::HOTKEY_KEYBOARD_BIND, action
                    ));

                    // Clear button if bound (only show on hover)
                    if (binding.hasKeyboard() && isRowHovered) {
                        float clearX = kbX + charWidth * (kbFieldWidth + 2.5f);
                        addString("x", clearX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getNegative(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            clearX, currentY, charWidth * 2, dim.lineHeightNormal,
                            ClickRegion::HOTKEY_KEYBOARD_CLEAR, action
                        ));
                    }
                }

                // Controller binding
                bool isCapturingController = isCapturing && captureAction == action && captureType == CaptureType::CONTROLLER;
                float ctrlX = controllerX;

                if (isCapturingController) {
                    // Show capture prompt (accent color)
                    char capturePrompt[32];
                    snprintf(capturePrompt, sizeof(capturePrompt), "[%-*s]", ctrlFieldWidth, "Press Btn...");
                    addString(capturePrompt, ctrlX, currentY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                } else {
                    // Show current binding
                    const char* btnName = getControllerButtonName(binding.controller);

                    // Format as clickable: [binding] - wider field, truncate if too long
                    char displayStr[32];
                    snprintf(displayStr, sizeof(displayStr), "[%-*.*s]", ctrlFieldWidth, ctrlFieldWidth, btnName);

                    // Determine color: hovered > bound > unbound
                    bool isCtrlHovered = (m_hoveredHotkeyRow == currentRowIndex && m_hoveredHotkeyColumn == HotkeyColumn::CONTROLLER);
                    unsigned long btnColor;
                    if (isCtrlHovered) {
                        btnColor = ColorConfig::getInstance().getAccent();
                    } else if (binding.hasController()) {
                        btnColor = ColorConfig::getInstance().getPrimary();
                    } else {
                        btnColor = ColorConfig::getInstance().getMuted();
                    }
                    addString(displayStr, ctrlX, currentY, Justify::LEFT,
                        Fonts::getNormal(), btnColor, dim.fontSize);

                    // Click region for controller binding (covers full field)
                    m_clickRegions.push_back(ClickRegion(
                        ctrlX, currentY, charWidth * (ctrlFieldWidth + 2), dim.lineHeightNormal,
                        ClickRegion::HOTKEY_CONTROLLER_BIND, action
                    ));

                    // Clear button if bound (only show on hover)
                    if (binding.hasController() && isRowHovered) {
                        float clearX = ctrlX + charWidth * (ctrlFieldWidth + 2.5f);
                        addString("x", clearX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getNegative(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            clearX, currentY, charWidth * 2, dim.lineHeightNormal,
                            ClickRegion::HOTKEY_CONTROLLER_CLEAR, action
                        ));
                    }
                }

                currentY += dim.lineHeightNormal;
                ++currentRowIndex;
            };

            // Settings Menu first
            addHotkeyRow(HotkeyAction::TOGGLE_SETTINGS);

            currentY += dim.lineHeightNormal * 0.5f;  // Spacing after settings

            // All HUD toggles
            addHotkeyRow(HotkeyAction::TOGGLE_STANDINGS);
            addHotkeyRow(HotkeyAction::TOGGLE_MAP);
            addHotkeyRow(HotkeyAction::TOGGLE_RADAR);
            addHotkeyRow(HotkeyAction::TOGGLE_LAP_LOG);
            addHotkeyRow(HotkeyAction::TOGGLE_IDEAL_LAP);
            addHotkeyRow(HotkeyAction::TOGGLE_TELEMETRY);
            addHotkeyRow(HotkeyAction::TOGGLE_INPUT);
            addHotkeyRow(HotkeyAction::TOGGLE_RECORDS);
            addHotkeyRow(HotkeyAction::TOGGLE_PITBOARD);
            addHotkeyRow(HotkeyAction::TOGGLE_TIMING);
            addHotkeyRow(HotkeyAction::TOGGLE_GAP_BAR);
            addHotkeyRow(HotkeyAction::TOGGLE_PERFORMANCE);
            addHotkeyRow(HotkeyAction::TOGGLE_RUMBLE);

            currentY += dim.lineHeightNormal * 0.5f;  // Spacing before All Widgets

            addHotkeyRow(HotkeyAction::TOGGLE_WIDGETS);
            addHotkeyRow(HotkeyAction::TOGGLE_ALL_HUDS);

            // Info text at bottom
            currentY += dim.lineHeightNormal * 0.5f;
            addString("Click to rebind, ESC to cancel", actionX, currentY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize * 0.9f);
        }
        break;

        case TAB_STANDINGS:
            activeHud = m_standings;
            dataStartY = addHudControls(m_standings);

            // RIGHT COLUMN: HUD-specific controls and column configuration
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(14, dim.fontSize);
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                // Row count control (specific to StandingsHud)
                addString("Rows", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char rowCountValue[8];
                snprintf(rowCountValue, sizeof(rowCountValue), "%d", m_standings->m_displayRowCount);
                addCycleControl(toggleX, rightY, rowCountValue, 2,
                    ClickRegion::ROW_COUNT_DOWN, ClickRegion::ROW_COUNT_UP, m_standings);
                rightY += dim.lineHeightNormal;

                // Adjacent rider gaps mode
                addString("Adjacent Gaps", rightColumnX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                const char* gapRowsValue;
                switch (m_standings->m_gapIndicatorMode) {
                    case StandingsHud::GapIndicatorMode::OFF:      gapRowsValue = "Off     "; break;
                    case StandingsHud::GapIndicatorMode::OFFICIAL: gapRowsValue = "Official"; break;
                    case StandingsHud::GapIndicatorMode::LIVE:     gapRowsValue = "Live    "; break;
                    case StandingsHud::GapIndicatorMode::BOTH:     gapRowsValue = "Both    "; break;
                    default: gapRowsValue = "Off     "; break;
                }
                unsigned long gapRowsValueColor = (m_standings->m_gapIndicatorMode == StandingsHud::GapIndicatorMode::OFF)
                    ? ColorConfig::getInstance().getMuted()
                    : ColorConfig::getInstance().getPrimary();
                float gapCurrentX = toggleX;
                addString("<", gapCurrentX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    gapCurrentX, rightY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::GAP_INDICATOR_DOWN, &m_standings->m_gapIndicatorMode, m_standings
                ));
                gapCurrentX += charWidth * 2;
                addString(gapRowsValue, gapCurrentX, rightY, Justify::LEFT, Fonts::getNormal(), gapRowsValueColor, dim.fontSize);
                gapCurrentX += charWidth * 8;
                addString(" >", gapCurrentX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    gapCurrentX, rightY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::GAP_INDICATOR_UP, &m_standings->m_gapIndicatorMode, m_standings
                ));
                rightY += dim.lineHeightNormal;

                // Gap reference mode (gaps relative to leader or player)
                addString("Gap Reference", rightColumnX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                const char* gapRefValue = (m_standings->m_gapReferenceMode == StandingsHud::GapReferenceMode::LEADER)
                    ? "Leader" : "Player";
                float refCurrentX = toggleX;
                addString("<", refCurrentX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    refCurrentX, rightY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::GAP_REFERENCE_DOWN, &m_standings->m_gapReferenceMode, m_standings
                ));
                refCurrentX += charWidth * 2;
                addString(gapRefValue, refCurrentX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                refCurrentX += charWidth * 6;
                addString(" >", refCurrentX, rightY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    refCurrentX, rightY, charWidth * 2, dim.lineHeightNormal,
                    ClickRegion::GAP_REFERENCE_UP, &m_standings->m_gapReferenceMode, m_standings
                ));
                rightY += dim.lineHeightNormal;
                rightY += dim.lineHeightNormal * 0.5f;  // Extra spacing before column table

                // Column configuration table: Column | Enabled
                float tableY = rightY;
                float columnNameX = rightColumnX;

                // Table header
                addString("Column", columnNameX, tableY, Justify::LEFT, Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Enabled", toggleX, tableY, Justify::LEFT, Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                tableY += dim.lineHeightNormal;

                // Table rows - each row shows column name and state
                struct ColumnRow { const char* name; uint32_t flag; bool isGapColumn; };
                ColumnRow columns[] = {
                    {"Tracked",      StandingsHud::COL_TRACKED,     false},
                    {"Position",     StandingsHud::COL_POS,         false},
                    {"Race #",       StandingsHud::COL_RACENUM,     false},
                    {"Name",         StandingsHud::COL_NAME,        false},
                    {"Bike",         StandingsHud::COL_BIKE,        false},
                    {"Status",       StandingsHud::COL_STATUS,      false},
                    {"Penalty",      StandingsHud::COL_PENALTY,     false},
                    {"Best Lap",     StandingsHud::COL_BEST_LAP,    false},
                    {"Official Gap", StandingsHud::COL_OFFICIAL_GAP, true},  // Multi-state gap column
                    {"Live Gap",     StandingsHud::COL_LIVE_GAP,    true}    // Multi-state gap column
                };

                // Helper lambda to add gap mode click regions (separate for < and >)
                auto addGapModeRegions = [&](float x, float y, StandingsHud::GapMode* modePtr) {
                    // Left arrow "<" - GAP_MODE_DOWN
                    m_clickRegions.push_back(ClickRegion(
                        x, y, charWidth * 2, dim.lineHeightNormal,
                        ClickRegion::GAP_MODE_DOWN, modePtr, m_standings
                    ));
                    // Right arrow ">" - GAP_MODE_UP (after "< Player " = 2 + 6 = 8 chars)
                    m_clickRegions.push_back(ClickRegion(
                        x + charWidth * 8, y, charWidth * 2, dim.lineHeightNormal,
                        ClickRegion::GAP_MODE_UP, modePtr, m_standings
                    ));
                };

                // Helper lambda to add column toggle region
                auto addColumnToggle = [&](float x, float y, uint32_t* bitfield, uint32_t flag) {
                    ClickRegion region;
                    region.x = x;
                    region.y = y;
                    region.width = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);  // "< On  >"
                    region.height = dim.lineHeightNormal;
                    region.type = ClickRegion::CHECKBOX;
                    region.targetPointer = bitfield;
                    region.flagBit = flag;
                    region.isRequired = false;
                    region.targetHud = m_standings;
                    region.tabIndex = 0;
                    m_clickRegions.push_back(region);
                };

                for (const auto& col : columns) {
                    // Column name
                    addString(col.name, columnNameX, tableY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

                    float currentX = toggleX;

                    if (col.isGapColumn) {
                        // Gap column - show < Off/Player/All > with accent arrows
                        StandingsHud::GapMode* gapModePtr = (col.flag == StandingsHud::COL_OFFICIAL_GAP)
                            ? &m_standings->m_officialGapMode
                            : &m_standings->m_liveGapMode;
                        const char* value;
                        switch (*gapModePtr) {
                            case StandingsHud::GapMode::OFF:    value = "Off   "; break;
                            case StandingsHud::GapMode::PLAYER: value = "Player"; break;
                            case StandingsHud::GapMode::ALL:    value = "All   "; break;
                            default: value = "Off   "; break;
                        }
                        unsigned long valueColor = (*gapModePtr == StandingsHud::GapMode::OFF)
                            ? ColorConfig::getInstance().getMuted()
                            : ColorConfig::getInstance().getPrimary();
                        addString("<", currentX, tableY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        currentX += charWidth * 2;
                        addString(value, currentX, tableY, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
                        currentX += charWidth * 6;
                        addString(" >", currentX, tableY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        addGapModeRegions(toggleX, tableY, gapModePtr);
                    } else {
                        // Regular column - show < On/Off > with accent arrows
                        bool enabled = (m_standings->m_enabledColumns & col.flag) != 0;
                        unsigned long valueColor = enabled
                            ? ColorConfig::getInstance().getPrimary()
                            : ColorConfig::getInstance().getMuted();
                        addString("<", currentX, tableY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        currentX += charWidth * 2;
                        addString(enabled ? "On " : "Off", currentX, tableY, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
                        currentX += charWidth * 3;
                        addString(" >", currentX, tableY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        addColumnToggle(toggleX, tableY, &m_standings->m_enabledColumns, col.flag);
                    }

                    tableY += dim.lineHeightNormal;
                }
            }
            break;

        case TAB_MAP:
            activeHud = m_mapHud;
            dataStartY = addHudControls(m_mapHud);

            // RIGHT COLUMN: Map-specific controls
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Range control (Full = no zoom, or zoom distance in meters) - TOP for visibility
                addString("Range", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char rangeValue[16];
                if (m_mapHud->getZoomEnabled()) {
                    snprintf(rangeValue, sizeof(rangeValue), "%.0fm", m_mapHud->getZoomDistance());
                } else {
                    snprintf(rangeValue, sizeof(rangeValue), "Full");
                }
                addCycleControl(toggleX, rightY, rangeValue, 5,  // "1000m" or "Full"
                    ClickRegion::MAP_RANGE_DOWN, ClickRegion::MAP_RANGE_UP, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Rotation toggle
                addString("Rotate", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_mapHud->getRotateToPlayer(),
                    ClickRegion::MAP_ROTATION_TOGGLE, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Outline toggle
                addString("Outline", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_mapHud->getShowOutline(),
                    ClickRegion::MAP_OUTLINE_TOGGLE, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Rider color mode cycle
                const char* mapColorModeStr = "";
                switch (m_mapHud->getRiderColorMode()) {
                    case MapHud::RiderColorMode::UNIFORM:      mapColorModeStr = "Uniform"; break;
                    case MapHud::RiderColorMode::BRAND:        mapColorModeStr = "Brand"; break;
                    case MapHud::RiderColorMode::RELATIVE_POS: mapColorModeStr = "Position"; break;
                }
                addString("Colors", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, mapColorModeStr, 8,  // "Position" is longest
                    ClickRegion::MAP_COLORIZE_DOWN, ClickRegion::MAP_COLORIZE_UP, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Track line width scale controls
                addString("Width", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char trackWidthValue[16];
                snprintf(trackWidthValue, sizeof(trackWidthValue), "%.0f%%", m_mapHud->getTrackWidthScale() * 100.0f);
                addCycleControl(toggleX, rightY, trackWidthValue, 4,  // "200%"
                    ClickRegion::MAP_TRACK_WIDTH_DOWN, ClickRegion::MAP_TRACK_WIDTH_UP, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Label mode control
                const char* modeStr = "";
                bool labelIsOff = (m_mapHud->getLabelMode() == MapHud::LabelMode::NONE);
                switch (m_mapHud->getLabelMode()) {
                    case MapHud::LabelMode::NONE:     modeStr = "Off"; break;
                    case MapHud::LabelMode::POSITION: modeStr = "Position"; break;
                    case MapHud::LabelMode::RACE_NUM: modeStr = "Race Num"; break;
                    case MapHud::LabelMode::BOTH:     modeStr = "Both"; break;
                    default:
                        DEBUG_WARN_F("Unknown LabelMode: %d", static_cast<int>(m_mapHud->getLabelMode()));
                        modeStr = "Unknown";
                        break;
                }
                addString("Labels", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, modeStr, 8,  // "Race Num" is longest
                    ClickRegion::MAP_LABEL_MODE_DOWN, ClickRegion::MAP_LABEL_MODE_UP, m_mapHud, true, labelIsOff);
                rightY += dim.lineHeightNormal;

                // Rider shape control
                const char* shapeStr = "";
                bool shapeIsOff = (m_mapHud->getRiderShape() == MapHud::RiderShape::OFF);
                switch (m_mapHud->getRiderShape()) {
                    case MapHud::RiderShape::OFF:        shapeStr = "Off"; break;
                    case MapHud::RiderShape::ARROWUP:    shapeStr = "ArrowUp"; break;
                    case MapHud::RiderShape::CHEVRON:    shapeStr = "Chevron"; break;
                    case MapHud::RiderShape::CIRCLE:     shapeStr = "Circle"; break;
                    case MapHud::RiderShape::CIRCLEPLAY: shapeStr = "CirclePlay"; break;
                    case MapHud::RiderShape::CIRCLEUP:   shapeStr = "CircleUp"; break;
                    case MapHud::RiderShape::DOT:        shapeStr = "Dot"; break;
                    case MapHud::RiderShape::LOCATION:   shapeStr = "Location"; break;
                    case MapHud::RiderShape::PIN:        shapeStr = "Pin"; break;
                    case MapHud::RiderShape::PLANE:      shapeStr = "Plane"; break;
                    case MapHud::RiderShape::VINYL:      shapeStr = "Vinyl"; break;
                }
                addString("Riders", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, shapeStr, 10,  // "CirclePlay" is longest
                    ClickRegion::MAP_RIDER_SHAPE_DOWN, ClickRegion::MAP_RIDER_SHAPE_UP, m_mapHud, true, shapeIsOff);
                rightY += dim.lineHeightNormal;

                // Marker scale control (independent scale for icons/labels)
                addString("Markers", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char mapMarkerScaleValue[16];
                snprintf(mapMarkerScaleValue, sizeof(mapMarkerScaleValue), "%.0f%%", m_mapHud->getMarkerScale() * 100.0f);
                addCycleControl(toggleX, rightY, mapMarkerScaleValue, 4,  // "200%"
                    ClickRegion::MAP_MARKER_SCALE_DOWN, ClickRegion::MAP_MARKER_SCALE_UP, m_mapHud);
                rightY += dim.lineHeightNormal;
            }
            break;

        case TAB_LAP_LOG:
            activeHud = m_lapLog;
            dataStartY = addHudControls(m_lapLog);

            // RIGHT COLUMN: HUD-specific controls and data toggles
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Row count control (specific to LapLogHud)
                addString("Rows", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char rowCountValue[8];
                snprintf(rowCountValue, sizeof(rowCountValue), "%d", m_lapLog->m_maxDisplayLaps);
                addCycleControl(toggleX, rightY, rowCountValue, 2,
                    ClickRegion::LAP_LOG_ROW_COUNT_DOWN, ClickRegion::LAP_LOG_ROW_COUNT_UP, m_lapLog);
                rightY += dim.lineHeightNormal;  // Move to next row

                // Data toggles
                addDataToggle("Lap #", &m_lapLog->m_enabledColumns, LapLogHud::COL_LAP, false, m_lapLog, rightY);
                addGroupToggle("Sectors", &m_lapLog->m_enabledColumns,
                    LapLogHud::COL_S1 | LapLogHud::COL_S2 | LapLogHud::COL_S3,
                    false, m_lapLog, rightY + dim.lineHeightNormal);
                addDataToggle("Time", &m_lapLog->m_enabledColumns, LapLogHud::COL_TIME, false, m_lapLog, rightY + dim.lineHeightNormal * 2);
            }
            break;

        case TAB_IDEAL_LAP:
            activeHud = m_idealLap;
            dataStartY = addHudControls(m_idealLap);
            // Right column data toggles (2 items)
            addGroupToggle("Sectors", &m_idealLap->m_enabledRows,
                IdealLapHud::ROW_S1 | IdealLapHud::ROW_S2 | IdealLapHud::ROW_S3,
                false, m_idealLap, dataStartY);
            addGroupToggle("Laps", &m_idealLap->m_enabledRows,
                IdealLapHud::ROW_LAST | IdealLapHud::ROW_BEST | IdealLapHud::ROW_IDEAL,
                false, m_idealLap, dataStartY + dim.lineHeightNormal);
            break;

        case TAB_TELEMETRY:
            activeHud = m_telemetry;
            dataStartY = addHudControls(m_telemetry);

            // RIGHT COLUMN: HUD-specific controls and data toggles
            {
                float rightY = dataStartY;

                // Display mode control (with cycle buttons)
                addDisplayModeControl(rightColumnX, rightY, dim, &m_telemetry->m_displayMode, m_telemetry);

                // Data toggles
                addDataToggle("Throttle", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_THROTTLE, false, m_telemetry, rightY);
                addDataToggle("Front Brake", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_FRONT_BRAKE, false, m_telemetry, rightY + dim.lineHeightNormal);
                addDataToggle("Rear Brake", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_REAR_BRAKE, false, m_telemetry, rightY + dim.lineHeightNormal * 2);
                addDataToggle("Clutch", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_CLUTCH, false, m_telemetry, rightY + dim.lineHeightNormal * 3);
                addDataToggle("RPM", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_RPM, false, m_telemetry, rightY + dim.lineHeightNormal * 4);
                addDataToggle("Front Susp", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_FRONT_SUSP, false, m_telemetry, rightY + dim.lineHeightNormal * 5);
                addDataToggle("Rear Susp", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_REAR_SUSP, false, m_telemetry, rightY + dim.lineHeightNormal * 6);
                addDataToggle("Gear", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_GEAR, false, m_telemetry, rightY + dim.lineHeightNormal * 7);
            }
            break;

        case TAB_INPUT:
            activeHud = m_input;
            dataStartY = addHudControls(m_input);
            // Right column data toggles (3 items)
            addDataToggle("Crosshairs", &m_input->m_enabledElements, InputHud::ELEM_CROSSHAIRS, false, m_input, dataStartY);
            addDataToggle("Trails", &m_input->m_enabledElements, InputHud::ELEM_TRAILS, false, m_input, dataStartY + dim.lineHeightNormal);
            addDataToggle("Numbers", &m_input->m_enabledElements, InputHud::ELEM_VALUES, false, m_input, dataStartY + dim.lineHeightNormal * 2);

            // Info text (placed after left column controls)
            currentY += dim.lineHeightNormal * 0.5f;
            addString("Select your controller in the General tab", leftColumnX, currentY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize * 0.9f);
            break;

        case TAB_PERFORMANCE:
            activeHud = m_performance;
            dataStartY = addHudControls(m_performance);

            // RIGHT COLUMN: HUD-specific controls and data toggles
            {
                float rightY = dataStartY;

                // Display mode control (with cycle buttons)
                addDisplayModeControl(rightColumnX, rightY, dim, &m_performance->m_displayMode, m_performance);

                // Data toggles
                addDataToggle("FPS", &m_performance->m_enabledElements, PerformanceHud::ELEM_FPS, false, m_performance, rightY);
                addDataToggle("CPU", &m_performance->m_enabledElements, PerformanceHud::ELEM_CPU, false, m_performance, rightY + dim.lineHeightNormal);
            }
            break;

        case TAB_PITBOARD:
            activeHud = m_pitboard;
            dataStartY = addHudControls(m_pitboard, false);  // No title support

            // RIGHT COLUMN: HUD-specific controls and data toggles
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Display mode control (Always/Pit/Splits)
                const char* displayModeText = "";
                if (m_pitboard->m_displayMode == PitboardHud::MODE_ALWAYS) {
                    displayModeText = "Always";
                } else if (m_pitboard->m_displayMode == PitboardHud::MODE_PIT) {
                    displayModeText = "Pit";
                } else if (m_pitboard->m_displayMode == PitboardHud::MODE_SPLITS) {
                    displayModeText = "Splits";
                }
                addString("Show", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, displayModeText, 6,  // "Always" is longest
                    ClickRegion::PITBOARD_SHOW_MODE_DOWN, ClickRegion::PITBOARD_SHOW_MODE_UP, m_pitboard);
                rightY += dim.lineHeightNormal;  // Move to next row

                // Data toggles
                addDataToggle("Rider", &m_pitboard->m_enabledRows, PitboardHud::ROW_RIDER_ID, false, m_pitboard, rightY);
                addDataToggle("Session", &m_pitboard->m_enabledRows, PitboardHud::ROW_SESSION, false, m_pitboard, rightY + dim.lineHeightNormal);
                addDataToggle("Position", &m_pitboard->m_enabledRows, PitboardHud::ROW_POSITION, false, m_pitboard, rightY + dim.lineHeightNormal * 2);
                addDataToggle("Time", &m_pitboard->m_enabledRows, PitboardHud::ROW_TIME, false, m_pitboard, rightY + dim.lineHeightNormal * 3);
                addDataToggle("Lap", &m_pitboard->m_enabledRows, PitboardHud::ROW_LAP, false, m_pitboard, rightY + dim.lineHeightNormal * 4);
                addDataToggle("Last Lap", &m_pitboard->m_enabledRows, PitboardHud::ROW_LAST_LAP, false, m_pitboard, rightY + dim.lineHeightNormal * 5);
                addDataToggle("Gap", &m_pitboard->m_enabledRows, PitboardHud::ROW_GAP, false, m_pitboard, rightY + dim.lineHeightNormal * 6);
            }
            break;

        case TAB_RECORDS:
            activeHud = m_records;
            dataStartY = addHudControls(m_records);  // Visibility/title/scale/opacity controls

            // RIGHT COLUMN: HUD-specific controls and data toggles
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Rows count control
                addString("Rows", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char recordsValue[8];
                snprintf(recordsValue, sizeof(recordsValue), "%d", m_records->m_recordsToShow);
                addCycleControl(toggleX, rightY, recordsValue, 2,
                    ClickRegion::RECORDS_COUNT_DOWN, ClickRegion::RECORDS_COUNT_UP, m_records);
                rightY += dim.lineHeightNormal;  // Move to next row

                // Data toggles
                addDataToggle("Position", &m_records->m_enabledColumns, RecordsHud::COL_POS, false, m_records, rightY);
                addDataToggle("Rider", &m_records->m_enabledColumns, RecordsHud::COL_RIDER, false, m_records, rightY + dim.lineHeightNormal);
                addDataToggle("Bike", &m_records->m_enabledColumns, RecordsHud::COL_BIKE, false, m_records, rightY + dim.lineHeightNormal * 2);
                addDataToggle("Laptime", &m_records->m_enabledColumns, RecordsHud::COL_LAPTIME, false, m_records, rightY + dim.lineHeightNormal * 3);
                addDataToggle("Date", &m_records->m_enabledColumns, RecordsHud::COL_DATE, false, m_records, rightY + dim.lineHeightNormal * 4);
            }
            break;

        case TAB_TIMING:
            activeHud = m_timing;
            dataStartY = addHudControls(m_timing, false);  // No title support (center display)

            // RIGHT COLUMN: TimingHud-specific controls
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Helper to get mode text
                auto getModeText = [](ColumnMode mode) -> const char* {
                    switch (mode) {
                        case ColumnMode::OFF: return "Off";
                        case ColumnMode::SPLITS: return "Splits";
                        case ColumnMode::ALWAYS: return "Always";
                        default: return "?";
                    }
                };

                // Per-column mode controls
                addString("Label", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, getModeText(m_timing->m_columnModes[TimingHud::COL_LABEL]), 6,
                    ClickRegion::TIMING_LABEL_MODE_DOWN, ClickRegion::TIMING_LABEL_MODE_UP, m_timing,
                    true, m_timing->m_columnModes[TimingHud::COL_LABEL] == ColumnMode::OFF);
                rightY += dim.lineHeightNormal;

                addString("Time", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, getModeText(m_timing->m_columnModes[TimingHud::COL_TIME]), 6,
                    ClickRegion::TIMING_TIME_MODE_DOWN, ClickRegion::TIMING_TIME_MODE_UP, m_timing,
                    true, m_timing->m_columnModes[TimingHud::COL_TIME] == ColumnMode::OFF);
                rightY += dim.lineHeightNormal;

                addString("Gap", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, getModeText(m_timing->m_columnModes[TimingHud::COL_GAP]), 6,
                    ClickRegion::TIMING_GAP_MODE_DOWN, ClickRegion::TIMING_GAP_MODE_UP, m_timing,
                    true, m_timing->m_columnModes[TimingHud::COL_GAP] == ColumnMode::OFF);
                rightY += dim.lineHeightNormal;

                // Freeze control (freeze duration for official times)
                char freezeValue[16];
                bool freezeIsOff = (m_timing->m_displayDurationMs == 0);
                if (freezeIsOff) {
                    strcpy_s(freezeValue, sizeof(freezeValue), "Off");
                } else {
                    snprintf(freezeValue, sizeof(freezeValue), "%.1fs", m_timing->m_displayDurationMs / 1000.0f);
                }
                addString("Freeze", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, freezeValue, 4,  // "10.0s" is longest
                    ClickRegion::TIMING_DURATION_DOWN, ClickRegion::TIMING_DURATION_UP, m_timing, true, freezeIsOff);
                rightY += dim.lineHeightNormal;

                // Gap type toggles - which gap comparisons to show
                // Only show these controls if gap column is not off
                bool gapColumnEnabled = (m_timing->m_columnModes[TimingHud::COL_GAP] != ColumnMode::OFF);
                if (gapColumnEnabled) {
                    rightY += dim.lineHeightNormal * 0.5f;  // Small gap before section

                    addString("Gap Types", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                    rightY += dim.lineHeightNormal;

                    // Gap to PB toggle
                    bool gapPBEnabled = m_timing->isGapTypeEnabled(GAP_TO_PB);
                    addString("PB", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                    addToggleControl(toggleX, rightY, gapPBEnabled,
                        ClickRegion::TIMING_GAP_PB_TOGGLE, m_timing);
                    rightY += dim.lineHeightNormal;

                    // Gap to Ideal toggle
                    bool gapIdealEnabled = m_timing->isGapTypeEnabled(GAP_TO_IDEAL);
                    addString("Ideal", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                    addToggleControl(toggleX, rightY, gapIdealEnabled,
                        ClickRegion::TIMING_GAP_IDEAL_TOGGLE, m_timing);
                    rightY += dim.lineHeightNormal;

                    // Gap to Session Best toggle
                    bool gapSessionEnabled = m_timing->isGapTypeEnabled(GAP_TO_SESSION);
                    addString("Session", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                    addToggleControl(toggleX, rightY, gapSessionEnabled,
                        ClickRegion::TIMING_GAP_SESSION_TOGGLE, m_timing);
                }
            }
            break;

        case TAB_GAP_BAR:
            activeHud = m_gapBar;
            dataStartY = addHudControls(m_gapBar, false);  // No title support

            // RIGHT COLUMN: GapBarHud-specific controls
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Markers toggle (show both position markers)
                addString("Markers", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_gapBar->m_showMarkers,
                    ClickRegion::GAPBAR_MARKER_TOGGLE, m_gapBar);
                rightY += dim.lineHeightNormal;

                // Width control (bar width percentage)
                char widthValue[16];
                snprintf(widthValue, sizeof(widthValue), "%d%%", m_gapBar->m_barWidthPercent);
                addString("Width", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, widthValue, 4,
                    ClickRegion::GAPBAR_WIDTH_DOWN, ClickRegion::GAPBAR_WIDTH_UP, m_gapBar);
                rightY += dim.lineHeightNormal;

                // Range control (how much time fits from center to edge)
                char rangeValue[16];
                snprintf(rangeValue, sizeof(rangeValue), "%.1fs", m_gapBar->m_gapRangeMs / 1000.0f);
                addString("Range", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, rangeValue, 4,
                    ClickRegion::GAPBAR_RANGE_DOWN, ClickRegion::GAPBAR_RANGE_UP, m_gapBar);
                rightY += dim.lineHeightNormal;

                // Freeze control (freeze duration for official times)
                char freezeValue[16];
                bool gapFreezeIsOff = (m_gapBar->m_freezeDurationMs == 0);
                if (gapFreezeIsOff) {
                    strcpy_s(freezeValue, sizeof(freezeValue), "Off");
                } else {
                    snprintf(freezeValue, sizeof(freezeValue), "%.1fs", m_gapBar->m_freezeDurationMs / 1000.0f);
                }
                addString("Freeze", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, freezeValue, 4,
                    ClickRegion::GAPBAR_FREEZE_DOWN, ClickRegion::GAPBAR_FREEZE_UP, m_gapBar, true, gapFreezeIsOff);
            }
            break;

        case TAB_WIDGETS:
            // Widgets tab - table format with header row
            {
                // Table header - columns must match addWidgetRow positions exactly
                float nameX = leftColumnX;
                float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);
                float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize);   // Match addWidgetRow
                float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // Match addWidgetRow
                float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // Match addWidgetRow
                float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize); // Match addWidgetRow

                addString("Widget", nameX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Visible", visX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Title", titleX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Texture", bgTexX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Opacity", opacityX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Scale", scaleX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                currentY += dim.lineHeightNormal;

                // Widget rows
                // Parameters: name, hud, enableTitle, enableOpacity, enableScale, enableVisibility, enableBgTexture
                addWidgetRow("Lap", m_lap);
                addWidgetRow("Position", m_position);
                addWidgetRow("Time", m_time);
                addWidgetRow("Session", m_session);
                addWidgetRow("Speed", m_speed, false);  // No title for speed widget
                addWidgetRow("Speedo", m_speedo, false);  // No title for speedo widget
                addWidgetRow("Tacho", m_tacho, false);  // No title for tacho widget
                addWidgetRow("Bars", m_bars, false);  // No title for bars widget
                addWidgetRow("Notices", m_notices, false);  // No title for notices widget
                addWidgetRow("Fuel", m_fuel);  // Title enabled
                addWidgetRow("Pointer", m_pointer, false, false, true, false, true);  // Scale, bg texture (always visible, no opacity)
                addWidgetRow("Version", m_version, false, false, false, true, false);  // Only visibility toggle enabled

                // No active HUD for multi-widget tab
                activeHud = nullptr;
            }
            break;

        case TAB_RADAR:
            activeHud = m_radarHud;
            dataStartY = addHudControls(m_radarHud, false);  // No title support

            // RIGHT COLUMN: Radar-specific controls
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Range control
                addString("Range", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char rangeValue[16];
                snprintf(rangeValue, sizeof(rangeValue), "%.0fm", m_radarHud->getRadarRange());
                addCycleControl(toggleX, rightY, rangeValue, 4,  // "100m"
                    ClickRegion::RADAR_RANGE_DOWN, ClickRegion::RADAR_RANGE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Alert distance control (when triangles light up)
                addString("Alert", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char alertValue[16];
                snprintf(alertValue, sizeof(alertValue), "%.0fm", m_radarHud->getAlertDistance());
                addCycleControl(toggleX, rightY, alertValue, 4,  // "100m"
                    ClickRegion::RADAR_ALERT_DISTANCE_DOWN, ClickRegion::RADAR_ALERT_DISTANCE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Rider color mode cycle
                const char* radarColorModeStr = "";
                switch (m_radarHud->getRiderColorMode()) {
                    case RadarHud::RiderColorMode::UNIFORM:      radarColorModeStr = "Uniform"; break;
                    case RadarHud::RiderColorMode::BRAND:        radarColorModeStr = "Brand"; break;
                    case RadarHud::RiderColorMode::RELATIVE_POS: radarColorModeStr = "Position"; break;
                }
                addString("Colors", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, radarColorModeStr, 8,  // "Position" is longest
                    ClickRegion::RADAR_COLORIZE_DOWN, ClickRegion::RADAR_COLORIZE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Show player arrow toggle
                addString("Player", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_radarHud->getShowPlayerArrow(),
                    ClickRegion::RADAR_PLAYER_ARROW_TOGGLE, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Auto-hide toggle (fade when no riders nearby)
                addString("Auto-hide", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_radarHud->getFadeWhenEmpty(),
                    ClickRegion::RADAR_FADE_TOGGLE, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Label mode control
                const char* radarModeStr = "";
                bool radarLabelIsOff = (m_radarHud->getLabelMode() == RadarHud::LabelMode::NONE);
                switch (m_radarHud->getLabelMode()) {
                    case RadarHud::LabelMode::NONE:     radarModeStr = "Off"; break;
                    case RadarHud::LabelMode::POSITION: radarModeStr = "Position"; break;
                    case RadarHud::LabelMode::RACE_NUM: radarModeStr = "Race Num"; break;
                    case RadarHud::LabelMode::BOTH:     radarModeStr = "Both"; break;
                    default:
                        DEBUG_WARN_F("Unknown LabelMode: %d", static_cast<int>(m_radarHud->getLabelMode()));
                        radarModeStr = "Unknown";
                        break;
                }
                addString("Labels", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, radarModeStr, 8,  // "Race Num" is longest
                    ClickRegion::RADAR_LABEL_MODE_DOWN, ClickRegion::RADAR_LABEL_MODE_UP, m_radarHud, true, radarLabelIsOff);
                rightY += dim.lineHeightNormal;

                // Rider shape control (no Off option for radar - riders always shown)
                const char* radarShapeStr = "";
                switch (m_radarHud->getRiderShape()) {
                    case RadarHud::RiderShape::ARROWUP:    radarShapeStr = "ArrowUp"; break;
                    case RadarHud::RiderShape::CHEVRON:    radarShapeStr = "Chevron"; break;
                    case RadarHud::RiderShape::CIRCLE:     radarShapeStr = "Circle"; break;
                    case RadarHud::RiderShape::CIRCLEPLAY: radarShapeStr = "CirclePlay"; break;
                    case RadarHud::RiderShape::CIRCLEUP:   radarShapeStr = "CircleUp"; break;
                    case RadarHud::RiderShape::DOT:        radarShapeStr = "Dot"; break;
                    case RadarHud::RiderShape::LOCATION:   radarShapeStr = "Location"; break;
                    case RadarHud::RiderShape::PIN:        radarShapeStr = "Pin"; break;
                    case RadarHud::RiderShape::PLANE:      radarShapeStr = "Plane"; break;
                    case RadarHud::RiderShape::VINYL:      radarShapeStr = "Vinyl"; break;
                }
                addString("Riders", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, radarShapeStr, 10,  // "CirclePlay" is longest
                    ClickRegion::RADAR_RIDER_SHAPE_DOWN, ClickRegion::RADAR_RIDER_SHAPE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Marker scale control (independent scale for icons/labels)
                addString("Markers", rightColumnX, rightY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char radarMarkerScaleValue[16];
                snprintf(radarMarkerScaleValue, sizeof(radarMarkerScaleValue), "%.0f%%", m_radarHud->getMarkerScale() * 100.0f);
                addCycleControl(toggleX, rightY, radarMarkerScaleValue, 4,  // "200%"
                    ClickRegion::RADAR_MARKER_SCALE_DOWN, ClickRegion::RADAR_MARKER_SCALE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;
            }
            break;

        case TAB_RUMBLE:
            // Rumble settings tab - standard HUD controls + effects table
            {
                activeHud = m_rumble;
                dataStartY = addHudControls(m_rumble);

                RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

                // RIGHT COLUMN: Rumble-specific controls
                {
                    float rightY = dataStartY;
                    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(14, dim.fontSize);

                    // Master rumble enable (mirrors tab checkbox for clarity)
                    addString("Rumble", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                    addToggleControl(toggleX, rightY, rumbleConfig.enabled,
                        ClickRegion::RUMBLE_TOGGLE, nullptr);
                    rightY += dim.lineHeightNormal;

                    // Stack mode: On = effects add up, Off = max wins
                    addString("Stack Forces", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                    addToggleControl(toggleX, rightY, rumbleConfig.additiveBlend,
                        ClickRegion::RUMBLE_BLEND_TOGGLE, nullptr);
                    rightY += dim.lineHeightNormal;

                    // Rumble when crashed control
                    addString("When Crashed", rightColumnX, rightY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
                    addToggleControl(toggleX, rightY, rumbleConfig.rumbleWhenCrashed,
                        ClickRegion::RUMBLE_CRASH_TOGGLE, nullptr);
                    rightY += dim.lineHeightNormal;
                }

                // Effects table below the standard controls
                currentY += dim.lineHeightNormal * 0.5f;  // Extra spacing before table

                // Table header - columns: Effect | Light | Heavy | Min | Max
                float effectX = leftColumnX;
                float lightX = effectX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize);
                float heavyX = lightX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize);
                float minX = heavyX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize);
                float maxX = minX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);  // Extra space after Min

                addString("Effect", effectX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Light", lightX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Heavy", heavyX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Min", minX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Max", maxX, currentY, Justify::LEFT,
                    Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                currentY += dim.lineHeightNormal;

                // Lambda for rumble effect rows
                // Parameters: name, effect, lightDown, lightUp, heavyDown, heavyUp, minDown, minUp, maxDown, maxUp, useIntegers, unit, displayFactor
                auto addRumbleRow = [&](const char* name, RumbleEffect& effect,
                                        ClickRegion::Type lightDown,
                                        ClickRegion::Type lightUp,
                                        ClickRegion::Type heavyDown,
                                        ClickRegion::Type heavyUp,
                                        ClickRegion::Type minDown,
                                        ClickRegion::Type minUp,
                                        ClickRegion::Type maxDown,
                                        ClickRegion::Type maxUp,
                                        bool useIntegers = false,
                                        const char* unit = "",
                                        float displayFactor = 1.0f) {
                    // Effect name
                    addString(name, effectX, currentY, Justify::LEFT,
                        Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);

                    // Light motor strength control: < XX% > or < Off >
                    {
                        char valueStr[8];
                        int percent = static_cast<int>(std::round(effect.lightStrength * 100.0f));
                        if (percent <= 0) {
                            snprintf(valueStr, sizeof(valueStr), "%-4s", "Off");
                        } else {
                            char tempStr[8];
                            snprintf(tempStr, sizeof(tempStr), "%d%%", percent);
                            snprintf(valueStr, sizeof(valueStr), "%-4s", tempStr);
                        }
                        float currentX = lightX;
                        addString("<", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            lightDown, nullptr
                        ));
                        currentX += charWidth * 2;
                        addString(valueStr, currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), effect.lightStrength > 0 ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
                        currentX += charWidth * 4;
                        addString(" >", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            lightUp, nullptr
                        ));
                    }

                    // Heavy motor strength control: < XX% > or < Off >
                    {
                        char valueStr[8];
                        int percent = static_cast<int>(std::round(effect.heavyStrength * 100.0f));
                        if (percent <= 0) {
                            snprintf(valueStr, sizeof(valueStr), "%-4s", "Off");
                        } else {
                            char tempStr[8];
                            snprintf(tempStr, sizeof(tempStr), "%d%%", percent);
                            snprintf(valueStr, sizeof(valueStr), "%-4s", tempStr);
                        }
                        float currentX = heavyX;
                        addString("<", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            heavyDown, nullptr
                        ));
                        currentX += charWidth * 2;
                        addString(valueStr, currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), effect.heavyStrength > 0 ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
                        currentX += charWidth * 4;
                        addString(" >", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            heavyUp, nullptr
                        ));
                    }

                    // Min input control: < X.X > or < X >
                    {
                        char valueStr[8];
                        float displayValue = effect.minInput * displayFactor;
                        // Round to nearest 5 when using display conversion (e.g., Surface in mph/km/h)
                        if (displayFactor != 1.0f) {
                            int rounded = static_cast<int>(std::round(displayValue / 5.0f)) * 5;
                            snprintf(valueStr, sizeof(valueStr), "%d", rounded);
                        } else if (useIntegers) {
                            snprintf(valueStr, sizeof(valueStr), "%d", static_cast<int>(std::round(displayValue)));
                        } else {
                            snprintf(valueStr, sizeof(valueStr), "%.1f", displayValue);
                        }
                        float currentX = minX;
                        addString("<", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            minDown, nullptr
                        ));
                        currentX += charWidth * 2;
                        addString(valueStr, currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                        currentX += charWidth * 6;  // Extra space for wide values like RPM
                        addString(">", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            minUp, nullptr
                        ));
                    }

                    // Max input control: < X.X > unit or < X > unit
                    {
                        char valueStr[8];
                        float displayValue = effect.maxInput * displayFactor;
                        // Round to nearest 5 when using display conversion (e.g., Surface in mph/km/h)
                        if (displayFactor != 1.0f) {
                            int rounded = static_cast<int>(std::round(displayValue / 5.0f)) * 5;
                            snprintf(valueStr, sizeof(valueStr), "%d", rounded);
                        } else if (useIntegers) {
                            snprintf(valueStr, sizeof(valueStr), "%d", static_cast<int>(std::round(displayValue)));
                        } else {
                            snprintf(valueStr, sizeof(valueStr), "%.1f", displayValue);
                        }
                        float currentX = maxX;
                        addString("<", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            maxDown, nullptr
                        ));
                        currentX += charWidth * 2;
                        addString(valueStr, currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
                        currentX += charWidth * 6;  // Extra space for wide values like RPM
                        addString(">", currentX, currentY, Justify::LEFT,
                            Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
                        m_clickRegions.push_back(ClickRegion(
                            currentX, currentY, charWidth * 2, dim.lineHeightNormal,
                            maxUp, nullptr
                        ));
                        if (unit[0] != '\0') {
                            currentX += charWidth * 2;
                            addString(unit, currentX, currentY, Justify::LEFT,
                                Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
                        }
                    }

                    currentY += dim.lineHeightNormal;
                };

                // Effect rows (lightDown, lightUp, heavyDown, heavyUp, minDown, minUp, maxDown, maxUp, useIntegers, unit)
                addRumbleRow("Bumps", rumbleConfig.suspensionEffect,
                    ClickRegion::RUMBLE_SUSP_LIGHT_DOWN, ClickRegion::RUMBLE_SUSP_LIGHT_UP,
                    ClickRegion::RUMBLE_SUSP_HEAVY_DOWN, ClickRegion::RUMBLE_SUSP_HEAVY_UP,
                    ClickRegion::RUMBLE_SUSP_MIN_DOWN, ClickRegion::RUMBLE_SUSP_MIN_UP,
                    ClickRegion::RUMBLE_SUSP_MAX_DOWN, ClickRegion::RUMBLE_SUSP_MAX_UP, true, "m/s");
                addRumbleRow("Slide", rumbleConfig.slideEffect,
                    ClickRegion::RUMBLE_SLIDE_LIGHT_DOWN, ClickRegion::RUMBLE_SLIDE_LIGHT_UP,
                    ClickRegion::RUMBLE_SLIDE_HEAVY_DOWN, ClickRegion::RUMBLE_SLIDE_HEAVY_UP,
                    ClickRegion::RUMBLE_SLIDE_MIN_DOWN, ClickRegion::RUMBLE_SLIDE_MIN_UP,
                    ClickRegion::RUMBLE_SLIDE_MAX_DOWN, ClickRegion::RUMBLE_SLIDE_MAX_UP, true, "deg");
                addRumbleRow("Spin", rumbleConfig.wheelspinEffect,
                    ClickRegion::RUMBLE_WHEEL_LIGHT_DOWN, ClickRegion::RUMBLE_WHEEL_LIGHT_UP,
                    ClickRegion::RUMBLE_WHEEL_HEAVY_DOWN, ClickRegion::RUMBLE_WHEEL_HEAVY_UP,
                    ClickRegion::RUMBLE_WHEEL_MIN_DOWN, ClickRegion::RUMBLE_WHEEL_MIN_UP,
                    ClickRegion::RUMBLE_WHEEL_MAX_DOWN, ClickRegion::RUMBLE_WHEEL_MAX_UP, true, "x");
                addRumbleRow("Lockup", rumbleConfig.brakeLockupEffect,
                    ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN, ClickRegion::RUMBLE_LOCKUP_LIGHT_UP,
                    ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN, ClickRegion::RUMBLE_LOCKUP_HEAVY_UP,
                    ClickRegion::RUMBLE_LOCKUP_MIN_DOWN, ClickRegion::RUMBLE_LOCKUP_MIN_UP,
                    ClickRegion::RUMBLE_LOCKUP_MAX_DOWN, ClickRegion::RUMBLE_LOCKUP_MAX_UP, false, "ratio");
                addRumbleRow("Wheelie", rumbleConfig.wheelieEffect,
                    ClickRegion::RUMBLE_WHEELIE_LIGHT_DOWN, ClickRegion::RUMBLE_WHEELIE_LIGHT_UP,
                    ClickRegion::RUMBLE_WHEELIE_HEAVY_DOWN, ClickRegion::RUMBLE_WHEELIE_HEAVY_UP,
                    ClickRegion::RUMBLE_WHEELIE_MIN_DOWN, ClickRegion::RUMBLE_WHEELIE_MIN_UP,
                    ClickRegion::RUMBLE_WHEELIE_MAX_DOWN, ClickRegion::RUMBLE_WHEELIE_MAX_UP, true, "deg");
                addRumbleRow("Steer", rumbleConfig.steerEffect,
                    ClickRegion::RUMBLE_STEER_LIGHT_DOWN, ClickRegion::RUMBLE_STEER_LIGHT_UP,
                    ClickRegion::RUMBLE_STEER_HEAVY_DOWN, ClickRegion::RUMBLE_STEER_HEAVY_UP,
                    ClickRegion::RUMBLE_STEER_MIN_DOWN, ClickRegion::RUMBLE_STEER_MIN_UP,
                    ClickRegion::RUMBLE_STEER_MAX_DOWN, ClickRegion::RUMBLE_STEER_MAX_UP, true, "Nm");
                addRumbleRow("RPM", rumbleConfig.rpmEffect,
                    ClickRegion::RUMBLE_RPM_LIGHT_DOWN, ClickRegion::RUMBLE_RPM_LIGHT_UP,
                    ClickRegion::RUMBLE_RPM_HEAVY_DOWN, ClickRegion::RUMBLE_RPM_HEAVY_UP,
                    ClickRegion::RUMBLE_RPM_MIN_DOWN, ClickRegion::RUMBLE_RPM_MIN_UP,
                    ClickRegion::RUMBLE_RPM_MAX_DOWN, ClickRegion::RUMBLE_RPM_MAX_UP, true, "rpm");
                // Surface uses user's speed unit preference
                {
                    bool isKmh = m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
                    const char* surfaceUnit = isKmh ? "km/h" : "mph";
                    float surfaceFactor = isKmh ? 3.6f : 2.23694f;  // m/s to km/h or mph
                    addRumbleRow("Surface", rumbleConfig.surfaceEffect,
                        ClickRegion::RUMBLE_SURFACE_LIGHT_DOWN, ClickRegion::RUMBLE_SURFACE_LIGHT_UP,
                        ClickRegion::RUMBLE_SURFACE_HEAVY_DOWN, ClickRegion::RUMBLE_SURFACE_HEAVY_UP,
                        ClickRegion::RUMBLE_SURFACE_MIN_DOWN, ClickRegion::RUMBLE_SURFACE_MIN_UP,
                        ClickRegion::RUMBLE_SURFACE_MAX_DOWN, ClickRegion::RUMBLE_SURFACE_MAX_UP, true, surfaceUnit, surfaceFactor);
                }

                // Info text
                currentY += dim.lineHeightNormal * 0.5f;
                addString("Select your controller in the General tab", leftColumnX, currentY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize * 0.9f);
            }
            break;

        case TAB_RIDERS:
        {
            // Tracked Riders tab - two-section layout:
            // Top: Server players grid (clickable to add)
            // Bottom: Tracked riders with icon (left=color, right=shape), hover shows remove on right
            TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
            const PluginData& pluginData = PluginData::getInstance();
            float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            const ColorConfig& colors = ColorConfig::getInstance();

            // Use normal font for grid content (readable size)
            float gridFontSize = dim.fontSize;
            float gridLineHeight = dim.lineHeightNormal;
            float gridCharWidth = charWidth;

            // Grid layout constants - 3 columns with pagination
            constexpr int SERVER_PLAYERS_PER_ROW = 3;
            constexpr int SERVER_PLAYERS_ROWS = 6;
            constexpr int SERVER_PLAYERS_PER_PAGE = SERVER_PLAYERS_PER_ROW * SERVER_PLAYERS_ROWS;  // 18 per page
            constexpr int TRACKED_PER_ROW = 3;
            constexpr int TRACKED_ROWS = 12;
            constexpr int TRACKED_PER_PAGE = TRACKED_PER_ROW * TRACKED_ROWS;  // 36 per page

            // Calculate available content width (same method as version number)
            float rightEdgeX = contentStartX + panelWidth - dim.paddingH - dim.paddingH;
            float availableGridWidth = rightEdgeX - leftColumnX;

            // Calculate cell dimensions based on available width
            float serverCellWidth = availableGridWidth / SERVER_PLAYERS_PER_ROW;
            float trackedCellWidth = availableGridWidth / TRACKED_PER_ROW;

            // Calculate cell chars for name truncation (cell width in chars)
            int serverCellChars = static_cast<int>(serverCellWidth / gridCharWidth);
            int trackedCellChars = static_cast<int>(trackedCellWidth / gridCharWidth);

            // Server cell format: "#123 Name" - race num takes 5 chars, 1 char buffer, rest for name
            int serverNameChars = serverCellChars - 6;  // 5 = "#" + 3 digits + space, +1 buffer
            if (serverNameChars < 5) serverNameChars = 5;  // Minimum name length

            // Tracked cell format: "[ico] Name-" - icon takes 3 chars, remove takes 2, 1 char buffer
            int trackedNameChars = trackedCellChars - 6;  // 3 for icon, 2 for remove, 1 buffer
            if (trackedNameChars < 5) trackedNameChars = 5;  // Minimum name length

            float cellHeight = gridLineHeight;

            // Helper lambda to render pagination controls (reduces duplication)
            // Returns the updated Y position after rendering
            auto renderPagination = [&](float& y, int currentPage, int totalPages,
                                        ClickRegion::Type prevType, ClickRegion::Type nextType) {
                if (totalPages <= 1) return;

                y += dim.lineHeightNormal * 0.5f;  // Gap before pagination
                char pageText[16];
                snprintf(pageText, sizeof(pageText), "Page %d/%d", currentPage + 1, totalPages);
                float pageTextWidth = PluginUtils::calculateMonospaceTextWidth(
                    static_cast<int>(strlen(pageText)), gridFontSize);

                // Position pagination at right edge
                // Format: "< Page x/y >" with spaces around arrows
                float paginationTotalWidth = gridCharWidth * 2 + pageTextWidth + gridCharWidth * 2;
                float paginationX = rightEdgeX - paginationTotalWidth;

                // "< " button
                addString("< ", paginationX, y, Justify::LEFT, Fonts::getNormal(),
                         colors.getAccent(), gridFontSize);
                m_clickRegions.push_back(ClickRegion(paginationX, y, gridCharWidth * 2, cellHeight,
                    prevType, nullptr, 0, false, 0));
                paginationX += gridCharWidth * 2;

                // Page text
                addString(pageText, paginationX, y, Justify::LEFT, Fonts::getNormal(),
                         colors.getSecondary(), gridFontSize);
                paginationX += pageTextWidth;

                // " >" button
                addString(" >", paginationX, y, Justify::LEFT, Fonts::getNormal(),
                         colors.getAccent(), gridFontSize);
                m_clickRegions.push_back(ClickRegion(paginationX, y, gridCharWidth * 2, cellHeight,
                    nextType, nullptr, 0, false, 0));

                y += dim.lineHeightNormal;
            };

            // =====================================================
            // SECTION 1: Server Players Grid
            // =====================================================
            addString("Server Players", leftColumnX, currentY, Justify::LEFT,
                Fonts::getStrong(), colors.getPrimary(), dim.fontSize);
            addString("(click to track/untrack)", leftColumnX + charWidth * 16, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getMuted(), dim.fontSize * 0.9f);
            currentY += dim.lineHeightNormal;

            // Get all race entries and build display list
            const auto& raceEntries = pluginData.getRaceEntries();
            std::vector<const RaceEntryData*> serverPlayers;
            for (const auto& pair : raceEntries) {
                serverPlayers.push_back(&pair.second);
            }

            // Sort by race number
            std::sort(serverPlayers.begin(), serverPlayers.end(),
                [](const RaceEntryData* a, const RaceEntryData* b) {
                    return a->raceNum < b->raceNum;
                });

            // Calculate total server players and pagination
            int totalServerPlayers = static_cast<int>(serverPlayers.size());
            int serverTotalPages = (totalServerPlayers + SERVER_PLAYERS_PER_PAGE - 1) / SERVER_PLAYERS_PER_PAGE;
            if (serverTotalPages < 1) serverTotalPages = 1;
            if (m_serverPlayersPage >= serverTotalPages) m_serverPlayersPage = serverTotalPages - 1;
            if (m_serverPlayersPage < 0) m_serverPlayersPage = 0;
            int serverStartIndex = m_serverPlayersPage * SERVER_PLAYERS_PER_PAGE;

            // Render server players grid (current page only)
            float serverGridStartY = currentY;
            for (int row = 0; row < SERVER_PLAYERS_ROWS; row++) {
                float rowY = serverGridStartY + row * cellHeight;
                for (int col = 0; col < SERVER_PLAYERS_PER_ROW; col++) {
                    int playerIndex = serverStartIndex + row * SERVER_PLAYERS_PER_ROW + col;
                    if (playerIndex >= totalServerPlayers) break;

                    float cellX = leftColumnX + col * serverCellWidth;
                    const RaceEntryData* player = serverPlayers[playerIndex];
                    bool isTracked = trackedMgr.isTracked(player->name);

                    // Format: "#123 Name" (dynamic width based on available space)
                    char cellText[48];
                    snprintf(cellText, sizeof(cellText), "#%-3d %-*.*s", player->raceNum, serverNameChars, serverNameChars, player->name);

                    unsigned long textColor = isTracked ? colors.getPositive() : colors.getSecondary();
                    addString(cellText, cellX, rowY, Justify::LEFT,
                        Fonts::getNormal(), textColor, gridFontSize);

                    // Click region to add/remove tracking
                    if (isTracked) {
                        m_clickRegions.push_back(ClickRegion(
                            cellX, rowY, serverCellWidth, cellHeight,
                            ClickRegion::RIDER_REMOVE, std::string(player->name)
                        ));
                    } else {
                        m_clickRegions.push_back(ClickRegion(
                            cellX, rowY, serverCellWidth, cellHeight,
                            ClickRegion::RIDER_ADD, std::string(player->name)
                        ));
                    }
                }
            }
            currentY = serverGridStartY + SERVER_PLAYERS_ROWS * cellHeight;

            // Server pagination
            renderPagination(currentY, m_serverPlayersPage, serverTotalPages,
                            ClickRegion::SERVER_PAGE_PREV, ClickRegion::SERVER_PAGE_NEXT);

            currentY += dim.lineHeightNormal * 0.3f;

            // =====================================================
            // SECTION 2: Tracked Riders Grid
            // =====================================================
            addString("Tracked Riders", leftColumnX, currentY, Justify::LEFT,
                Fonts::getStrong(), colors.getPrimary(), dim.fontSize);
            addString("(L-click: color, R-click: icon)", leftColumnX + charWidth * 16, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getMuted(), dim.fontSize * 0.9f);
            currentY += dim.lineHeightNormal;

            // Get tracked riders
            const auto& allTracked = trackedMgr.getAllTrackedRiders();
            std::vector<const TrackedRiderConfig*> trackedList;
            for (const auto& pair : allTracked) {
                trackedList.push_back(&pair.second);
            }

            // Sort tracked by name
            std::sort(trackedList.begin(), trackedList.end(),
                [](const TrackedRiderConfig* a, const TrackedRiderConfig* b) {
                    return a->name < b->name;
                });

            // Calculate total tracked riders and pagination
            int totalTrackedRiders = static_cast<int>(trackedList.size());
            int trackedTotalPages = (totalTrackedRiders + TRACKED_PER_PAGE - 1) / TRACKED_PER_PAGE;
            if (trackedTotalPages < 1) trackedTotalPages = 1;
            if (m_trackedRidersPage >= trackedTotalPages) m_trackedRidersPage = trackedTotalPages - 1;
            if (m_trackedRidersPage < 0) m_trackedRidersPage = 0;
            int trackedStartIndex = m_trackedRidersPage * TRACKED_PER_PAGE;

            // Store layout info for hover tracking
            m_trackedRidersStartY = currentY;
            m_trackedRidersStartX = leftColumnX;
            m_trackedRidersCellHeight = cellHeight;
            m_trackedRidersCellWidth = trackedCellWidth;
            m_trackedRidersPerRow = TRACKED_PER_ROW;

            // Sprite sizing - match StandingsHud icon size (0.006f base)
            constexpr float baseConeSize = 0.006f;
            float baseHalfSize = baseConeSize;  // Same as StandingsHud

            // Render tracked riders grid (current page only)
            float trackedGridStartY = currentY;
            for (int row = 0; row < TRACKED_ROWS; row++) {
                float rowY = trackedGridStartY + row * cellHeight;
                for (int col = 0; col < TRACKED_PER_ROW; col++) {
                    int trackedIndex = trackedStartIndex + row * TRACKED_PER_ROW + col;
                    if (trackedIndex >= totalTrackedRiders) break;

                    float cellX = leftColumnX + col * trackedCellWidth;
                    const TrackedRiderConfig* config = trackedList[trackedIndex];
                    const std::string& riderName = config->name;
                    unsigned long riderColor = config->color;
                    int shapeIndex = config->shapeIndex;

                    // Adjust hover index for pagination
                    int displayIndex = trackedIndex - trackedStartIndex;
                    bool isHovered = (displayIndex == m_hoveredTrackedRiderIndex);

                    float x = cellX;

                    // Icon sprite (clickable for color on left-click, icon on right-click)
                    {
                        float spriteHalfSize = baseHalfSize;
                        int spriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + shapeIndex - 1;

                        float spriteCenterX = x + gridCharWidth * 1.5f;  // Center icon in 3-char space
                        float spriteCenterY = rowY + cellHeight * 0.5f;
                        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;

                        SPluginQuad_t sprite;
                        float sx = spriteCenterX, sy = spriteCenterY;
                        applyOffset(sx, sy);
                        sprite.m_aafPos[0][0] = sx - spriteHalfWidth;
                        sprite.m_aafPos[0][1] = sy - spriteHalfSize;
                        sprite.m_aafPos[1][0] = sx - spriteHalfWidth;
                        sprite.m_aafPos[1][1] = sy + spriteHalfSize;
                        sprite.m_aafPos[2][0] = sx + spriteHalfWidth;
                        sprite.m_aafPos[2][1] = sy + spriteHalfSize;
                        sprite.m_aafPos[3][0] = sx + spriteHalfWidth;
                        sprite.m_aafPos[3][1] = sy - spriteHalfSize;
                        sprite.m_iSprite = spriteIndex;
                        sprite.m_ulColor = riderColor;
                        m_quads.push_back(sprite);

                        // Click region for color cycling (left-click) and shape cycling (right-click)
                        // Covers icon + name area (icon3 + nameChars = trackedCellChars - 2 for remove)
                        m_clickRegions.push_back(ClickRegion(
                            x, rowY, gridCharWidth * (3 + trackedNameChars), cellHeight,
                            ClickRegion::RIDER_COLOR_NEXT, riderName
                        ));
                    }
                    x += gridCharWidth * 3;  // Space for icon (3 chars)

                    // Name (dynamic width based on available space)
                    char truncName[48];
                    snprintf(truncName, sizeof(truncName), "%-*.*s", trackedNameChars, trackedNameChars, riderName.c_str());
                    addString(truncName, x, rowY, Justify::LEFT,
                        Fonts::getNormal(), riderColor, gridFontSize);

                    // Remove "x" only shown on hover, fixed at right edge of cell
                    if (isHovered) {
                        float removeX = cellX + trackedCellWidth - gridCharWidth * 2;
                        addString("x", removeX, rowY, Justify::LEFT,
                            Fonts::getNormal(), colors.getNegative(), gridFontSize);
                        m_clickRegions.push_back(ClickRegion(
                            removeX, rowY, gridCharWidth * 2, cellHeight,
                            ClickRegion::RIDER_REMOVE, riderName
                        ));
                    }
                }
            }
            currentY = trackedGridStartY + TRACKED_ROWS * cellHeight;

            // Tracked pagination
            renderPagination(currentY, m_trackedRidersPage, trackedTotalPages,
                            ClickRegion::TRACKED_PAGE_PREV, ClickRegion::TRACKED_PAGE_NEXT);
        }
        break;

        default:
            DEBUG_WARN_F("Invalid tab index: %d, defaulting to TAB_STANDINGS", m_activeTab);
            activeHud = m_standings;
            break;
    }

    currentY += sectionSpacing;

    // [Close] button at bottom center
    float closeButtonBottomY = startY + backgroundHeight - dim.paddingV - dim.lineHeightNormal;
    float closeButtonBottomX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
    float closeButtonWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
    float closeButtonX = closeButtonBottomX - closeButtonWidth / 2.0f;  // Properly center background

    // Add click region first to get index for hover check
    size_t closeRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        closeButtonX, closeButtonBottomY, closeButtonWidth, dim.lineHeightNormal,
        ClickRegion::CLOSE_BUTTON, nullptr, 0, false, 0
    ));

    // Button background
    {
        SPluginQuad_t bgQuad;
        float bgX = closeButtonX, bgY = closeButtonBottomY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, closeButtonWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
            ? ColorConfig::getInstance().getAccent()
            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        m_quads.push_back(bgQuad);
    }

    // Button text - PRIMARY when hovered, SECONDARY when not
    unsigned long closeTextColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getSecondary();
    addString("[Close]", closeButtonBottomX, closeButtonBottomY, Justify::CENTER,
        Fonts::getStrong(), closeTextColor, dim.fontSize);

    // [Reset Tab] button - bottom left corner
    float resetTabButtonY = closeButtonBottomY;
    float resetTabButtonWidth = PluginUtils::calculateMonospaceTextWidth(RESET_TAB_BUTTON_WIDTH, dim.fontSize);
    float resetTabButtonX = contentStartX;

    // Add click region first for hover check
    size_t resetTabRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        resetTabButtonX, resetTabButtonY, resetTabButtonWidth, dim.lineHeightNormal,
        ClickRegion::RESET_TAB_BUTTON, nullptr
    ));

    // Reset Tab button background
    {
        SPluginQuad_t bgQuad;
        float bgX = resetTabButtonX, bgY = resetTabButtonY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, resetTabButtonWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
            ? ColorConfig::getInstance().getAccent()
            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        m_quads.push_back(bgQuad);
    }

    // Reset Tab button text - PRIMARY when hovered, SECONDARY when not
    unsigned long resetTabTextColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getSecondary();
    addString("[Reset Tab]", resetTabButtonX + resetTabButtonWidth / 2.0f, resetTabButtonY, Justify::CENTER,
        Fonts::getNormal(), resetTabTextColor, dim.fontSize);

    // Version + update status display - bottom right corner
    {
        float versionY = closeButtonBottomY;
        float rightEdgeX = contentStartX + panelWidth - dim.paddingH - dim.paddingH;

        // Build version/status string based on update state
        char versionStr[64];
        unsigned long versionColor = ColorConfig::getInstance().getMuted();

        if (!UpdateChecker::getInstance().isEnabled()) {
            // Updates disabled - just show version
            snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
        } else {
            // Sync status from UpdateChecker (in case check was triggered on startup)
            switch (UpdateChecker::getInstance().getStatus()) {
                case UpdateChecker::Status::IDLE:
                    // No change - keep current status
                    break;
                case UpdateChecker::Status::CHECKING:
                    m_updateStatus = UpdateStatus::CHECKING;
                    break;
                case UpdateChecker::Status::UP_TO_DATE:
                    m_updateStatus = UpdateStatus::UP_TO_DATE;
                    break;
                case UpdateChecker::Status::UPDATE_AVAILABLE:
                    m_updateStatus = UpdateStatus::UPDATE_AVAILABLE;
                    m_latestVersion = UpdateChecker::getInstance().getLatestVersion();
                    break;
                case UpdateChecker::Status::CHECK_FAILED:
                    m_updateStatus = UpdateStatus::CHECK_FAILED;
                    break;
            }

            // Updates enabled - show status
            switch (m_updateStatus) {
                case UpdateStatus::UNKNOWN:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    break;
                case UpdateStatus::CHECKING:
                    snprintf(versionStr, sizeof(versionStr), "Checking...");
                    versionColor = ColorConfig::getInstance().getSecondary();
                    break;
                case UpdateStatus::UP_TO_DATE:
                    snprintf(versionStr, sizeof(versionStr), "v%s up-to-date", PluginConstants::PLUGIN_VERSION);
                    versionColor = ColorConfig::getInstance().getMuted();
                    break;
                case UpdateStatus::UPDATE_AVAILABLE:
                    // m_latestVersion already has 'v' prefix from GitHub tag
                    snprintf(versionStr, sizeof(versionStr), "%s available!", m_latestVersion.c_str());
                    versionColor = ColorConfig::getInstance().getPositive();
                    break;
                case UpdateStatus::CHECK_FAILED:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    // Silent fail - just show version in muted
                    break;
            }
        }

        // Calculate width for right-alignment
        float versionWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(versionStr)), dim.fontSize);
        float versionX = rightEdgeX - versionWidth;

        // Draw version text
        addString(versionStr, versionX, versionY, Justify::LEFT,
            Fonts::getNormal(), versionColor, dim.fontSize);

        // Add click region for easter egg trigger
        ClickRegion versionRegion;
        versionRegion.x = versionX;
        versionRegion.y = versionY;
        versionRegion.width = versionWidth;
        versionRegion.height = dim.lineHeightNormal;
        versionRegion.type = ClickRegion::VERSION_CLICK;
        m_clickRegions.push_back(versionRegion);
    }
}

void SettingsHud::handleClick(float mouseX, float mouseY) {
    // Check each clickable region
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // Dispatch to appropriate handler
            switch (region.type) {
                case ClickRegion::CHECKBOX:
                    handleCheckboxClick(region);
                    break;
                case ClickRegion::GAP_MODE_UP:
                    handleGapModeClick(region, true);
                    break;
                case ClickRegion::GAP_MODE_DOWN:
                    handleGapModeClick(region, false);
                    break;
                case ClickRegion::GAP_INDICATOR_UP:
                    handleGapIndicatorClick(region, true);
                    break;
                case ClickRegion::GAP_INDICATOR_DOWN:
                    handleGapIndicatorClick(region, false);
                    break;
                case ClickRegion::GAP_REFERENCE_UP:
                    handleGapReferenceClick(region, true);
                    break;
                case ClickRegion::GAP_REFERENCE_DOWN:
                    handleGapReferenceClick(region, false);
                    break;
                case ClickRegion::HUD_TOGGLE:
                    handleHudToggleClick(region);
                    break;
                case ClickRegion::WIDGETS_TOGGLE:
                    {
                        HudManager& hudManager = HudManager::getInstance();
                        hudManager.setWidgetsEnabled(!hudManager.areWidgetsEnabled());
                        rebuildRenderData();
                        DEBUG_INFO_F("Widgets master toggle: %s", hudManager.areWidgetsEnabled() ? "enabled" : "disabled");
                    }
                    break;
                case ClickRegion::TITLE_TOGGLE:
                    handleTitleToggleClick(region);
                    break;
                case ClickRegion::TEXTURE_VARIANT_UP:
                    if (region.targetHud) {
                        region.targetHud->cycleTextureVariant(true);
                        rebuildRenderData();
                    }
                    break;
                case ClickRegion::TEXTURE_VARIANT_DOWN:
                    if (region.targetHud) {
                        region.targetHud->cycleTextureVariant(false);
                        rebuildRenderData();
                    }
                    break;
                case ClickRegion::BACKGROUND_OPACITY_UP:
                    handleOpacityClick(region, true);
                    break;
                case ClickRegion::BACKGROUND_OPACITY_DOWN:
                    handleOpacityClick(region, false);
                    break;
                case ClickRegion::SCALE_UP:
                    handleScaleClick(region, true);
                    break;
                case ClickRegion::SCALE_DOWN:
                    handleScaleClick(region, false);
                    break;
                case ClickRegion::ROW_COUNT_UP:
                    handleRowCountClick(region, true);
                    break;
                case ClickRegion::ROW_COUNT_DOWN:
                    handleRowCountClick(region, false);
                    break;
                case ClickRegion::LAP_LOG_ROW_COUNT_UP:
                    handleLapLogRowCountClick(region, true);
                    break;
                case ClickRegion::LAP_LOG_ROW_COUNT_DOWN:
                    handleLapLogRowCountClick(region, false);
                    break;
                case ClickRegion::MAP_ROTATION_TOGGLE:
                    handleMapRotationClick(region);
                    break;
                case ClickRegion::MAP_OUTLINE_TOGGLE:
                    handleMapOutlineClick(region);
                    break;
                case ClickRegion::MAP_COLORIZE_UP:
                    handleMapColorizeClick(region, true);
                    break;
                case ClickRegion::MAP_COLORIZE_DOWN:
                    handleMapColorizeClick(region, false);
                    break;
                case ClickRegion::MAP_TRACK_WIDTH_UP:
                    handleMapTrackWidthClick(region, true);
                    break;
                case ClickRegion::MAP_TRACK_WIDTH_DOWN:
                    handleMapTrackWidthClick(region, false);
                    break;
                case ClickRegion::MAP_LABEL_MODE_UP:
                    handleMapLabelModeClick(region, true);
                    break;
                case ClickRegion::MAP_LABEL_MODE_DOWN:
                    handleMapLabelModeClick(region, false);
                    break;
                case ClickRegion::MAP_RANGE_UP:
                    handleMapRangeClick(region, true);
                    break;
                case ClickRegion::MAP_RANGE_DOWN:
                    handleMapRangeClick(region, false);
                    break;
                case ClickRegion::MAP_RIDER_SHAPE_UP:
                    handleMapRiderShapeClick(region, true);
                    break;
                case ClickRegion::MAP_RIDER_SHAPE_DOWN:
                    handleMapRiderShapeClick(region, false);
                    break;
                case ClickRegion::MAP_MARKER_SCALE_UP:
                    handleMapMarkerScaleClick(region, true);
                    break;
                case ClickRegion::MAP_MARKER_SCALE_DOWN:
                    handleMapMarkerScaleClick(region, false);
                    break;
                case ClickRegion::RADAR_RANGE_UP:
                    handleRadarRangeClick(region, true);
                    break;
                case ClickRegion::RADAR_RANGE_DOWN:
                    handleRadarRangeClick(region, false);
                    break;
                case ClickRegion::RADAR_COLORIZE_UP:
                    handleRadarColorizeClick(region, true);
                    break;
                case ClickRegion::RADAR_COLORIZE_DOWN:
                    handleRadarColorizeClick(region, false);
                    break;
                case ClickRegion::RADAR_PLAYER_ARROW_TOGGLE:
                    if (m_radarHud) {
                        m_radarHud->setShowPlayerArrow(!m_radarHud->getShowPlayerArrow());
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RADAR_FADE_TOGGLE:
                    if (m_radarHud) {
                        m_radarHud->setFadeWhenEmpty(!m_radarHud->getFadeWhenEmpty());
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RADAR_ALERT_DISTANCE_UP:
                    handleRadarAlertDistanceClick(region, true);
                    break;
                case ClickRegion::RADAR_ALERT_DISTANCE_DOWN:
                    handleRadarAlertDistanceClick(region, false);
                    break;
                case ClickRegion::RADAR_LABEL_MODE_UP:
                    handleRadarLabelModeClick(region, true);
                    break;
                case ClickRegion::RADAR_LABEL_MODE_DOWN:
                    handleRadarLabelModeClick(region, false);
                    break;
                case ClickRegion::RADAR_RIDER_SHAPE_UP:
                    handleRadarRiderShapeClick(region, true);
                    break;
                case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
                    handleRadarRiderShapeClick(region, false);
                    break;
                case ClickRegion::RADAR_MARKER_SCALE_UP:
                    handleRadarMarkerScaleClick(region, true);
                    break;
                case ClickRegion::RADAR_MARKER_SCALE_DOWN:
                    handleRadarMarkerScaleClick(region, false);
                    break;
                case ClickRegion::DISPLAY_MODE_UP:
                    handleDisplayModeClick(region, true);
                    break;
                case ClickRegion::DISPLAY_MODE_DOWN:
                    handleDisplayModeClick(region, false);
                    break;
                case ClickRegion::RECORDS_COUNT_UP:
                    if (m_records && m_records->m_recordsToShow < 10) {
                        m_records->m_recordsToShow++;
                        m_records->setDataDirty();
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::RECORDS_COUNT_DOWN:
                    if (m_records && m_records->m_recordsToShow > 1) {
                        m_records->m_recordsToShow--;
                        m_records->setDataDirty();
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::PITBOARD_SHOW_MODE_UP:
                    handlePitboardShowModeClick(region, true);
                    break;
                case ClickRegion::PITBOARD_SHOW_MODE_DOWN:
                    handlePitboardShowModeClick(region, false);
                    break;
                case ClickRegion::TIMING_LABEL_MODE_UP:
                case ClickRegion::TIMING_LABEL_MODE_DOWN:
                    if (m_timing) {
                        // Cycle label column mode: Off -> Splits -> Always -> Off
                        auto& mode = m_timing->m_columnModes[TimingHud::COL_LABEL];
                        if (region.type == ClickRegion::TIMING_LABEL_MODE_UP) {
                            mode = static_cast<ColumnMode>((static_cast<int>(mode) + 1) % 3);
                        } else {
                            mode = static_cast<ColumnMode>((static_cast<int>(mode) + 2) % 3);
                        }
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_TIME_MODE_UP:
                case ClickRegion::TIMING_TIME_MODE_DOWN:
                    if (m_timing) {
                        // Cycle time column mode: Off -> Splits -> Always -> Off
                        auto& mode = m_timing->m_columnModes[TimingHud::COL_TIME];
                        if (region.type == ClickRegion::TIMING_TIME_MODE_UP) {
                            mode = static_cast<ColumnMode>((static_cast<int>(mode) + 1) % 3);
                        } else {
                            mode = static_cast<ColumnMode>((static_cast<int>(mode) + 2) % 3);
                        }
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_GAP_MODE_UP:
                case ClickRegion::TIMING_GAP_MODE_DOWN:
                    if (m_timing) {
                        // Cycle gap column mode: Off -> Splits -> Always -> Off
                        auto& mode = m_timing->m_columnModes[TimingHud::COL_GAP];
                        if (region.type == ClickRegion::TIMING_GAP_MODE_UP) {
                            mode = static_cast<ColumnMode>((static_cast<int>(mode) + 1) % 3);
                        } else {
                            mode = static_cast<ColumnMode>((static_cast<int>(mode) + 2) % 3);
                        }
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_DURATION_UP:
                    if (m_timing) {
                        m_timing->m_displayDurationMs = std::min(
                            m_timing->m_displayDurationMs + TimingHud::DURATION_STEP_MS,
                            TimingHud::MAX_DURATION_MS);
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_DURATION_DOWN:
                    if (m_timing) {
                        m_timing->m_displayDurationMs = std::max(
                            m_timing->m_displayDurationMs - TimingHud::DURATION_STEP_MS,
                            TimingHud::MIN_DURATION_MS);
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_GAP_PB_TOGGLE:
                    if (m_timing) {
                        m_timing->setGapType(GAP_TO_PB, !m_timing->isGapTypeEnabled(GAP_TO_PB));
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_GAP_IDEAL_TOGGLE:
                    if (m_timing) {
                        m_timing->setGapType(GAP_TO_IDEAL, !m_timing->isGapTypeEnabled(GAP_TO_IDEAL));
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::TIMING_GAP_SESSION_TOGGLE:
                    if (m_timing) {
                        m_timing->setGapType(GAP_TO_SESSION, !m_timing->isGapTypeEnabled(GAP_TO_SESSION));
                        m_timing->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_FREEZE_UP:
                    if (m_gapBar) {
                        m_gapBar->m_freezeDurationMs = std::min(
                            m_gapBar->m_freezeDurationMs + GapBarHud::FREEZE_STEP_MS,
                            GapBarHud::MAX_FREEZE_MS);
                        m_gapBar->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_FREEZE_DOWN:
                    if (m_gapBar) {
                        m_gapBar->m_freezeDurationMs = std::max(
                            m_gapBar->m_freezeDurationMs - GapBarHud::FREEZE_STEP_MS,
                            GapBarHud::MIN_FREEZE_MS);
                        m_gapBar->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_MARKER_TOGGLE:
                    if (m_gapBar) {
                        m_gapBar->m_showMarkers = !m_gapBar->m_showMarkers;
                        m_gapBar->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_MODE_CYCLE:
                    // Mode removed - gap bar now always uses gap-based display
                    break;
                case ClickRegion::GAPBAR_RANGE_UP:
                    if (m_gapBar) {
                        m_gapBar->m_gapRangeMs = std::min(
                            m_gapBar->m_gapRangeMs + GapBarHud::RANGE_STEP_MS,
                            GapBarHud::MAX_RANGE_MS);
                        m_gapBar->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_RANGE_DOWN:
                    if (m_gapBar) {
                        m_gapBar->m_gapRangeMs = std::max(
                            m_gapBar->m_gapRangeMs - GapBarHud::RANGE_STEP_MS,
                            GapBarHud::MIN_RANGE_MS);
                        m_gapBar->setDataDirty();
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_WIDTH_UP:
                    if (m_gapBar) {
                        m_gapBar->setBarWidth(m_gapBar->m_barWidthPercent + GapBarHud::WIDTH_STEP_PERCENT);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::GAPBAR_WIDTH_DOWN:
                    if (m_gapBar) {
                        m_gapBar->setBarWidth(m_gapBar->m_barWidthPercent - GapBarHud::WIDTH_STEP_PERCENT);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::COLOR_CYCLE_NEXT:
                    handleColorCycleClick(region, true);
                    break;
                case ClickRegion::COLOR_CYCLE_PREV:
                    handleColorCycleClick(region, false);
                    break;
                case ClickRegion::FONT_CATEGORY_NEXT:
                    if (auto* category = std::get_if<FontCategory>(&region.targetPointer)) {
                        FontConfig::getInstance().cycleFont(*category, true);
                        // Mark all HUDs as dirty so they rebuild with new fonts immediately
                        HudManager::getInstance().markAllHudsDirty();
                        rebuildRenderData();  // Update settings menu itself
                    }
                    break;
                case ClickRegion::FONT_CATEGORY_PREV:
                    if (auto* category = std::get_if<FontCategory>(&region.targetPointer)) {
                        FontConfig::getInstance().cycleFont(*category, false);
                        // Mark all HUDs as dirty so they rebuild with new fonts immediately
                        HudManager::getInstance().markAllHudsDirty();
                        rebuildRenderData();  // Update settings menu itself
                    }
                    break;
                case ClickRegion::SPEED_UNIT_TOGGLE:
                    if (m_speed) {
                        // Toggle between mph and km/h
                        auto currentUnit = m_speed->getSpeedUnit();
                        m_speed->setSpeedUnit(currentUnit == SpeedWidget::SpeedUnit::MPH
                            ? SpeedWidget::SpeedUnit::KMH
                            : SpeedWidget::SpeedUnit::MPH);
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::FUEL_UNIT_TOGGLE:
                    if (m_fuel) {
                        // Toggle between liters and gallons
                        auto currentUnit = m_fuel->getFuelUnit();
                        m_fuel->setFuelUnit(currentUnit == FuelWidget::FuelUnit::LITERS
                            ? FuelWidget::FuelUnit::GALLONS
                            : FuelWidget::FuelUnit::LITERS);
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::GRID_SNAP_TOGGLE:
                    {
                        // Toggle grid snapping
                        bool current = ColorConfig::getInstance().getGridSnapping();
                        ColorConfig::getInstance().setGridSnapping(!current);
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::UPDATE_CHECK_TOGGLE:
                    {
                        // Toggle update checking (uses UpdateChecker's persisted state)
                        bool newState = !UpdateChecker::getInstance().isEnabled();
                        UpdateChecker::getInstance().setEnabled(newState);

                        if (newState) {
                            // Set callback to update UI when check completes
                            UpdateChecker::getInstance().setCompletionCallback([this]() {
                                // Sync status from UpdateChecker
                                auto status = UpdateChecker::getInstance().getStatus();
                                switch (status) {
                                    case UpdateChecker::Status::UP_TO_DATE:
                                        m_updateStatus = UpdateStatus::UP_TO_DATE;
                                        break;
                                    case UpdateChecker::Status::UPDATE_AVAILABLE:
                                        m_updateStatus = UpdateStatus::UPDATE_AVAILABLE;
                                        m_latestVersion = UpdateChecker::getInstance().getLatestVersion();
                                        break;
                                    case UpdateChecker::Status::CHECK_FAILED:
                                        m_updateStatus = UpdateStatus::CHECK_FAILED;
                                        break;
                                    default:
                                        break;
                                }
                                setDataDirty();  // Trigger UI rebuild
                            });

                            // Start the check
                            m_updateStatus = UpdateStatus::CHECKING;
                            UpdateChecker::getInstance().checkForUpdates();
                        } else {
                            // When disabled, reset to unknown
                            m_updateStatus = UpdateStatus::UNKNOWN;
                            m_latestVersion = "";
                            UpdateChecker::getInstance().setCompletionCallback(nullptr);
                        }
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::PROFILE_CYCLE_UP:
                    {
                        ProfileType nextProfile = ProfileManager::getNextProfile(
                            ProfileManager::getInstance().getActiveProfile());
                        SettingsManager::getInstance().switchProfile(HudManager::getInstance(), nextProfile);
                        rebuildRenderData();
                    }
                    return;  // switchProfile() already saves, don't double-save
                case ClickRegion::PROFILE_CYCLE_DOWN:
                    {
                        ProfileType prevProfile = ProfileManager::getPreviousProfile(
                            ProfileManager::getInstance().getActiveProfile());
                        SettingsManager::getInstance().switchProfile(HudManager::getInstance(), prevProfile);
                        rebuildRenderData();
                    }
                    return;  // switchProfile() already saves, don't double-save
                case ClickRegion::AUTO_SWITCH_TOGGLE:
                    {
                        // Toggle auto-switch for profiles
                        bool current = ProfileManager::getInstance().isAutoSwitchEnabled();
                        ProfileManager::getInstance().setAutoSwitchEnabled(!current);
                        setDataDirty();  // Update settings display
                    }
                    break;
                case ClickRegion::COPY_TARGET_UP:
                    {
                        // Cycle forward through valid copy targets: -1 (none) -> 4 (All) -> 0-3 (profiles, skip current)
                        ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
                        int8_t activeIdx = static_cast<int8_t>(activeProfile);

                        if (m_copyTargetProfile == -1) {
                            m_copyTargetProfile = 4;  // All
                        } else if (m_copyTargetProfile == 4) {
                            // Start with first profile, skip if it's the active one
                            m_copyTargetProfile = 0;
                            if (m_copyTargetProfile == activeIdx) {
                                m_copyTargetProfile++;
                            }
                        } else {
                            m_copyTargetProfile++;
                            // Skip current profile
                            if (m_copyTargetProfile == activeIdx) {
                                m_copyTargetProfile++;
                            }
                            // Wrap back to -1 if past last profile
                            if (m_copyTargetProfile >= static_cast<int8_t>(ProfileType::COUNT)) {
                                m_copyTargetProfile = -1;
                            }
                        }
                        rebuildRenderData();
                    }
                    return;  // Don't save settings, just UI state change
                case ClickRegion::COPY_TARGET_DOWN:
                    {
                        // Cycle backward through valid copy targets
                        ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
                        int8_t activeIdx = static_cast<int8_t>(activeProfile);

                        if (m_copyTargetProfile == -1) {
                            // Go to last profile, skip if it's the active one
                            m_copyTargetProfile = static_cast<int8_t>(ProfileType::COUNT) - 1;
                            if (m_copyTargetProfile == activeIdx) {
                                m_copyTargetProfile--;
                            }
                        } else if (m_copyTargetProfile == 4) {
                            m_copyTargetProfile = -1;  // None
                        } else if (m_copyTargetProfile == 0) {
                            m_copyTargetProfile = 4;  // All
                        } else {
                            m_copyTargetProfile--;
                            // Skip current profile
                            if (m_copyTargetProfile == activeIdx) {
                                m_copyTargetProfile--;
                            }
                            // If we went below 0, go to All
                            if (m_copyTargetProfile < 0) {
                                m_copyTargetProfile = 4;
                            }
                        }
                        rebuildRenderData();
                    }
                    return;  // Don't save settings, just UI state change
                case ClickRegion::RESET_PROFILE_CHECKBOX:
                    m_resetProfileConfirmed = !m_resetProfileConfirmed;
                    if (m_resetProfileConfirmed) {
                        m_resetAllConfirmed = false;  // Mutual exclusion
                    }
                    rebuildRenderData();
                    return;  // Don't save settings, just UI state change
                case ClickRegion::RESET_ALL_CHECKBOX:
                    m_resetAllConfirmed = !m_resetAllConfirmed;
                    if (m_resetAllConfirmed) {
                        m_resetProfileConfirmed = false;  // Mutual exclusion
                    }
                    rebuildRenderData();
                    return;  // Don't save settings, just UI state change
                case ClickRegion::COPY_BUTTON:
                    if (m_copyTargetProfile != -1) {
                        if (m_copyTargetProfile == 4) {
                            // Copy to all other profiles
                            SettingsManager::getInstance().applyToAllProfiles(HudManager::getInstance());
                        } else {
                            // Copy to specific profile
                            ProfileType targetProfile = static_cast<ProfileType>(m_copyTargetProfile);
                            SettingsManager::getInstance().copyToProfile(HudManager::getInstance(), targetProfile);
                        }
                        m_copyTargetProfile = -1;  // Reset selection after action
                    }
                    break;
                case ClickRegion::RESET_BUTTON:
                    // Unified reset button - action depends on which checkbox is checked
                    if (m_resetProfileConfirmed) {
                        resetCurrentProfile();
                        m_resetProfileConfirmed = false;
                        DEBUG_INFO("Current profile reset to defaults");
                    } else if (m_resetAllConfirmed) {
                        resetToDefaults();
                        m_resetAllConfirmed = false;
                        DEBUG_INFO("All settings reset to defaults");
                    }
                    break;
                case ClickRegion::RESET_TAB_BUTTON:
                    {
                        resetCurrentTab();
                        DEBUG_INFO_F("Tab %d reset to defaults", m_activeTab);
                    }
                    break;
                case ClickRegion::TAB:
                    handleTabClick(region);
                    return;  // Don't save settings, just UI state change
                case ClickRegion::CLOSE_BUTTON:
                    handleCloseButtonClick();
                    return;  // Don't save settings, just close the menu

                // Controller/Rumble settings
                case ClickRegion::RUMBLE_TOGGLE:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        config.enabled = !config.enabled;
                        // When disabling rumble, stop vibration and hide the RumbleHud
                        // (but don't auto-show it when enabling - user must enable HUD separately)
                        if (!config.enabled) {
                            XInputReader::getInstance().stopVibration();
                            if (m_rumble) {
                                m_rumble->setVisible(false);
                            }
                        }
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_CONTROLLER_UP:
                    {
                        // Cycle: -1 (disabled) -> 0 -> 1 -> 2 -> 3 -> -1
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        config.controllerIndex = (config.controllerIndex + 2) % 5 - 1;  // Maps -1,0,1,2,3 -> 0,1,2,3,-1
                        XInputReader::getInstance().setControllerIndex(config.controllerIndex);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_CONTROLLER_DOWN:
                    {
                        // Cycle: -1 (disabled) <- 0 <- 1 <- 2 <- 3 <- -1
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        config.controllerIndex = (config.controllerIndex + 5) % 5 - 1;  // Maps -1,0,1,2,3 -> 3,-1,0,1,2
                        XInputReader::getInstance().setControllerIndex(config.controllerIndex);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_BLEND_TOGGLE:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        config.additiveBlend = !config.additiveBlend;
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_CRASH_TOGGLE:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        config.rumbleWhenCrashed = !config.rumbleWhenCrashed;
                        setDataDirty();
                    }
                    break;
                // Suspension effect - light/heavy strength controls
                case ClickRegion::RUMBLE_SUSP_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.suspensionEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.suspensionEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.suspensionEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.suspensionEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.suspensionEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.suspensionEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.suspensionEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.suspensionEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.suspensionEffect.minInput + 1.0f);
                        config.suspensionEffect.minInput = std::min(config.suspensionEffect.maxInput - 1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.suspensionEffect.minInput - 1.0f);
                        config.suspensionEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.suspensionEffect.maxInput + 1.0f);
                        config.suspensionEffect.maxInput = std::min(50.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SUSP_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.suspensionEffect.maxInput - 1.0f);
                        config.suspensionEffect.maxInput = std::max(config.suspensionEffect.minInput + 1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                // Wheelspin effect - light/heavy strength controls
                case ClickRegion::RUMBLE_WHEEL_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelspinEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.wheelspinEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelspinEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.wheelspinEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelspinEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.wheelspinEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelspinEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.wheelspinEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.wheelspinEffect.minInput + 1.0f);
                        config.wheelspinEffect.minInput = std::min(config.wheelspinEffect.maxInput - 1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.wheelspinEffect.minInput - 1.0f);
                        config.wheelspinEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.wheelspinEffect.maxInput + 1.0f);
                        config.wheelspinEffect.maxInput = std::min(30.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEEL_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.wheelspinEffect.maxInput - 1.0f);
                        config.wheelspinEffect.maxInput = std::max(config.wheelspinEffect.minInput + 1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                // Brake lockup effect - light/heavy strength controls
                case ClickRegion::RUMBLE_LOCKUP_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.minInput + 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.minInput = std::min(config.brakeLockupEffect.maxInput - 0.1f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.minInput - 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.maxInput + 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.maxInput = std::min(5.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_LOCKUP_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.brakeLockupEffect.maxInput - 0.1f) * 10.0f) / 10.0f;
                        config.brakeLockupEffect.maxInput = std::max(config.brakeLockupEffect.minInput + 0.1f, newVal);
                        setDataDirty();
                    }
                    break;
                // RPM effect - light/heavy strength controls
                case ClickRegion::RUMBLE_RPM_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.rpmEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.rpmEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.rpmEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.rpmEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.minInput + 1000.0f) / 1000.0f) * 1000.0f;
                        config.rpmEffect.minInput = std::min(config.rpmEffect.maxInput - 1000.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.minInput - 1000.0f) / 1000.0f) * 1000.0f;
                        config.rpmEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.maxInput + 1000.0f) / 1000.0f) * 1000.0f;
                        config.rpmEffect.maxInput = std::min(20000.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_RPM_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.rpmEffect.maxInput - 1000.0f) / 1000.0f) * 1000.0f;
                        config.rpmEffect.maxInput = std::max(config.rpmEffect.minInput + 1000.0f, newVal);
                        setDataDirty();
                    }
                    break;
                // Slide effect - light/heavy strength controls
                case ClickRegion::RUMBLE_SLIDE_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.slideEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.slideEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.slideEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.slideEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.slideEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.slideEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.slideEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.slideEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.slideEffect.minInput + 1.0f);
                        config.slideEffect.minInput = std::min(config.slideEffect.maxInput - 1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.slideEffect.minInput - 1.0f);
                        config.slideEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.slideEffect.maxInput + 1.0f);
                        config.slideEffect.maxInput = std::min(90.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SLIDE_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round(config.slideEffect.maxInput - 1.0f);
                        config.slideEffect.maxInput = std::max(config.slideEffect.minInput + 1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                // Surface effect - light/heavy strength controls
                case ClickRegion::RUMBLE_SURFACE_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.surfaceEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.surfaceEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.surfaceEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.surfaceEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.surfaceEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.surfaceEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.surfaceEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.surfaceEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        // Step of 5 in display units (mph or km/h), converted to m/s
                        bool isKmh = m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
                        float step = isKmh ? (5.0f / 3.6f) : (5.0f / 2.23694f);
                        float newVal = config.surfaceEffect.minInput + step;
                        config.surfaceEffect.minInput = std::min(config.surfaceEffect.maxInput - step, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        bool isKmh = m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
                        float step = isKmh ? (5.0f / 3.6f) : (5.0f / 2.23694f);
                        float newVal = config.surfaceEffect.minInput - step;
                        config.surfaceEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        bool isKmh = m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
                        float step = isKmh ? (5.0f / 3.6f) : (5.0f / 2.23694f);
                        float newVal = config.surfaceEffect.maxInput + step;
                        config.surfaceEffect.maxInput = std::min(50.0f, newVal);  // ~110 mph or ~180 km/h max
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_SURFACE_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        bool isKmh = m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
                        float step = isKmh ? (5.0f / 3.6f) : (5.0f / 2.23694f);
                        float newVal = config.surfaceEffect.maxInput - step;
                        config.surfaceEffect.maxInput = std::max(config.surfaceEffect.minInput + step, newVal);
                        setDataDirty();
                    }
                    break;
                // Steer effect - light/heavy strength controls
                case ClickRegion::RUMBLE_STEER_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.steerEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.steerEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.steerEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.steerEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.minInput + 5.0f) / 5.0f) * 5.0f;
                        config.steerEffect.minInput = std::min(config.steerEffect.maxInput - 5.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.minInput - 5.0f) / 5.0f) * 5.0f;
                        config.steerEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.maxInput + 5.0f) / 5.0f) * 5.0f;
                        config.steerEffect.maxInput = std::min(200.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_STEER_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.steerEffect.maxInput - 5.0f) / 5.0f) * 5.0f;
                        config.steerEffect.maxInput = std::max(config.steerEffect.minInput + 5.0f, newVal);
                        setDataDirty();
                    }
                    break;
                // Wheelie effect - light/heavy strength controls
                case ClickRegion::RUMBLE_WHEELIE_LIGHT_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.lightStrength + 0.1f) * 10.0f) / 10.0f;
                        config.wheelieEffect.lightStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_LIGHT_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.lightStrength - 0.1f) * 10.0f) / 10.0f;
                        config.wheelieEffect.lightStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_HEAVY_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.heavyStrength + 0.1f) * 10.0f) / 10.0f;
                        config.wheelieEffect.heavyStrength = std::min(1.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_HEAVY_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.heavyStrength - 0.1f) * 10.0f) / 10.0f;
                        config.wheelieEffect.heavyStrength = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_MIN_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.minInput + 5.0f) / 5.0f) * 5.0f;
                        config.wheelieEffect.minInput = std::min(config.wheelieEffect.maxInput - 5.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_MIN_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.minInput - 5.0f) / 5.0f) * 5.0f;
                        config.wheelieEffect.minInput = std::max(0.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_MAX_UP:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.maxInput + 5.0f) / 5.0f) * 5.0f;
                        config.wheelieEffect.maxInput = std::min(180.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_WHEELIE_MAX_DOWN:
                    {
                        RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
                        float newVal = std::round((config.wheelieEffect.maxInput - 5.0f) / 5.0f) * 5.0f;
                        config.wheelieEffect.maxInput = std::max(config.wheelieEffect.minInput + 5.0f, newVal);
                        setDataDirty();
                    }
                    break;
                case ClickRegion::RUMBLE_HUD_TOGGLE:
                    if (m_rumble) {
                        m_rumble->setVisible(!m_rumble->isVisible());
                        setDataDirty();
                    }
                    break;

                // Hotkey binding controls
                case ClickRegion::HOTKEY_KEYBOARD_BIND:
                    {
                        auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                        if (actionPtr) {
                            HotkeyManager::getInstance().startCapture(*actionPtr, CaptureType::KEYBOARD);
                            setDataDirty();
                        }
                    }
                    break;
                case ClickRegion::HOTKEY_CONTROLLER_BIND:
                    {
                        auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                        if (actionPtr) {
                            HotkeyManager::getInstance().startCapture(*actionPtr, CaptureType::CONTROLLER);
                            setDataDirty();
                        }
                    }
                    break;
                case ClickRegion::HOTKEY_KEYBOARD_CLEAR:
                    {
                        auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                        if (actionPtr) {
                            HotkeyManager::getInstance().clearKeyboardBinding(*actionPtr);
                            setDataDirty();
                        }
                    }
                    break;
                case ClickRegion::HOTKEY_CONTROLLER_CLEAR:
                    {
                        auto* actionPtr = std::get_if<HotkeyAction>(&region.targetPointer);
                        if (actionPtr) {
                            HotkeyManager::getInstance().clearControllerBinding(*actionPtr);
                            setDataDirty();
                        }
                    }
                    break;

                // Tracked Riders controls
                case ClickRegion::RIDER_ADD:
                    {
                        auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                        if (namePtr) {
                            TrackedRidersManager::getInstance().addTrackedRider(*namePtr);
                            rebuildRenderData();
                        }
                    }
                    break;
                case ClickRegion::RIDER_REMOVE:
                    {
                        auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                        if (namePtr) {
                            TrackedRidersManager::getInstance().removeTrackedRider(*namePtr);
                            rebuildRenderData();
                        }
                    }
                    break;
                case ClickRegion::RIDER_COLOR_PREV:
                    {
                        auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                        if (namePtr) {
                            TrackedRidersManager::getInstance().cycleTrackedRiderColor(*namePtr, false);
                            rebuildRenderData();
                        }
                    }
                    break;
                case ClickRegion::RIDER_COLOR_NEXT:
                    {
                        auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                        if (namePtr) {
                            TrackedRidersManager::getInstance().cycleTrackedRiderColor(*namePtr, true);
                            rebuildRenderData();
                        }
                    }
                    break;
                case ClickRegion::RIDER_SHAPE_PREV:
                    {
                        auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                        if (namePtr) {
                            TrackedRidersManager::getInstance().cycleTrackedRiderShape(*namePtr, false);
                            rebuildRenderData();
                        }
                    }
                    break;
                case ClickRegion::RIDER_SHAPE_NEXT:
                    {
                        auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                        if (namePtr) {
                            TrackedRidersManager::getInstance().cycleTrackedRiderShape(*namePtr, true);
                            rebuildRenderData();
                        }
                    }
                    break;

                // Pagination controls
                case ClickRegion::SERVER_PAGE_PREV:
                    if (m_serverPlayersPage > 0) {
                        m_serverPlayersPage--;
                        rebuildRenderData();
                    }
                    break;
                case ClickRegion::SERVER_PAGE_NEXT:
                    m_serverPlayersPage++;
                    rebuildRenderData();
                    break;
                case ClickRegion::TRACKED_PAGE_PREV:
                    if (m_trackedRidersPage > 0) {
                        m_trackedRidersPage--;
                        rebuildRenderData();
                    }
                    break;
                case ClickRegion::TRACKED_PAGE_NEXT:
                    m_trackedRidersPage++;
                    rebuildRenderData();
                    break;

                case ClickRegion::VERSION_CLICK:
                    {
                        long long currentTimeUs = DrawHandler::getCurrentTimeUs();
                        // Reset counter if timeout elapsed
                        if (m_versionClickCount > 0 && (currentTimeUs - m_lastVersionClickTimeUs) > EASTER_EGG_TIMEOUT_US) {
                            m_versionClickCount = 0;
                        }
                        m_versionClickCount++;
                        m_lastVersionClickTimeUs = currentTimeUs;
                        // Check if threshold reached
                        if (m_versionClickCount >= EASTER_EGG_CLICKS) {
                            m_versionClickCount = 0;
                            if (m_version) {
                                hide();  // Close settings before starting game
                                m_version->startGame();
                            }
                        }
                    }
                    break;

                default:
                    DEBUG_WARN_F("Unknown ClickRegion type: %d", static_cast<int>(region.type));
                    break;
            }

            // Save settings after any modification (except TAB and CLOSE_BUTTON)
            SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());

            return;  // Only process one click per frame
        }
    }
}

void SettingsHud::handleRightClick(float mouseX, float mouseY) {
    // Right-click handling for TAB_RIDERS - cycles shape on icon
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // On right-click, treat RIDER_COLOR_NEXT as shape cycle
            if (region.type == ClickRegion::RIDER_COLOR_NEXT) {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().cycleTrackedRiderShape(*namePtr, true);
                    rebuildRenderData();
                    SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());
                }
                return;
            }
        }
    }
}

void SettingsHud::resetToDefaults() {
    // Reset all HUDs to their constructor defaults
    if (m_idealLap) m_idealLap->resetToDefaults();
    if (m_lapLog) m_lapLog->resetToDefaults();
    if (m_standings) m_standings->resetToDefaults();
    if (m_performance) m_performance->resetToDefaults();
    if (m_telemetry) m_telemetry->resetToDefaults();
    if (m_input) m_input->resetToDefaults();
    if (m_mapHud) m_mapHud->resetToDefaults();
    if (m_radarHud) m_radarHud->resetToDefaults();
    if (m_pitboard) m_pitboard->resetToDefaults();
    if (m_records) m_records->resetToDefaults();
    if (m_timing) m_timing->resetToDefaults();
    if (m_gapBar) m_gapBar->resetToDefaults();

    // Reset all widgets to their constructor defaults
    if (m_lap) m_lap->resetToDefaults();
    if (m_position) m_position->resetToDefaults();
    if (m_time) m_time->resetToDefaults();
    if (m_session) m_session->resetToDefaults();
    if (m_speed) m_speed->resetToDefaults();
    if (m_speedo) m_speedo->resetToDefaults();
    if (m_tacho) m_tacho->resetToDefaults();
    if (m_notices) m_notices->resetToDefaults();
    if (m_bars) m_bars->resetToDefaults();
    if (m_version) m_version->resetToDefaults();
    if (m_fuel) m_fuel->resetToDefaults();
    if (m_pointer) m_pointer->resetToDefaults();

    // Reset settings button (managed by HudManager)
    HudManager::getInstance().getSettingsButtonWidget().resetToDefaults();

    // Reset rumble configuration and RumbleHud
    XInputReader::getInstance().getRumbleConfig().resetToDefaults();
    if (m_rumble) m_rumble->resetToDefaults();

    // Reset color configuration
    ColorConfig::getInstance().resetToDefaults();

    // Reset font configuration
    FontConfig::getInstance().resetToDefaults();

    // Reset hotkey bindings
    HotkeyManager::getInstance().resetToDefaults();

    // Reset global preferences (speed/fuel units)
    if (m_speed) m_speed->setSpeedUnit(SpeedWidget::SpeedUnit::MPH);
    if (m_fuel) m_fuel->setFuelUnit(FuelWidget::FuelUnit::LITERS);

    // Reset update checker to default (off)
    UpdateChecker::getInstance().setEnabled(false);
    m_updateStatus = UpdateStatus::UNKNOWN;
    m_latestVersion = "";

    // Update settings display
    rebuildRenderData();

    // Apply reset state to all profiles and save
    SettingsManager::getInstance().applyToAllProfiles(HudManager::getInstance());
}

void SettingsHud::resetCurrentTab() {
    // Reset only the HUD(s) associated with the current tab
    switch (m_activeTab) {
        case TAB_GENERAL:
            // General tab - reset global preferences
            XInputReader::getInstance().getRumbleConfig().controllerIndex = 0;
            XInputReader::getInstance().setControllerIndex(0);
            if (m_speed) m_speed->setSpeedUnit(SpeedWidget::SpeedUnit::MPH);
            if (m_fuel) m_fuel->setFuelUnit(FuelWidget::FuelUnit::LITERS);
            ColorConfig::getInstance().setGridSnapping(true);  // Reset grid snap
            // Reset update checker
            UpdateChecker::getInstance().setEnabled(false);
            m_updateStatus = UpdateStatus::UNKNOWN;
            m_latestVersion = "";
            break;
        case TAB_APPEARANCE:
            // Appearance tab - reset font and color configuration
            FontConfig::getInstance().resetToDefaults();
            ColorConfig::getInstance().resetToDefaults();
            // Mark all HUDs dirty so they pick up new colors
            if (m_idealLap) m_idealLap->setDataDirty();
            if (m_lapLog) m_lapLog->setDataDirty();
            if (m_standings) m_standings->setDataDirty();
            if (m_performance) m_performance->setDataDirty();
            if (m_telemetry) m_telemetry->setDataDirty();
            if (m_input) m_input->setDataDirty();
            if (m_mapHud) m_mapHud->setDataDirty();
            if (m_radarHud) m_radarHud->setDataDirty();
            if (m_pitboard) m_pitboard->setDataDirty();
            if (m_records) m_records->setDataDirty();
            if (m_timing) m_timing->setDataDirty();
            if (m_gapBar) m_gapBar->setDataDirty();
            if (m_lap) m_lap->setDataDirty();
            if (m_position) m_position->setDataDirty();
            if (m_time) m_time->setDataDirty();
            if (m_session) m_session->setDataDirty();
            if (m_speed) m_speed->setDataDirty();
            if (m_speedo) m_speedo->setDataDirty();
            if (m_tacho) m_tacho->setDataDirty();
            if (m_notices) m_notices->setDataDirty();
            if (m_bars) m_bars->setDataDirty();
            if (m_version) m_version->setDataDirty();
            if (m_fuel) m_fuel->setDataDirty();
            break;
        case TAB_STANDINGS:
            if (m_standings) m_standings->resetToDefaults();
            break;
        case TAB_MAP:
            if (m_mapHud) m_mapHud->resetToDefaults();
            break;
        case TAB_RADAR:
            if (m_radarHud) m_radarHud->resetToDefaults();
            break;
        case TAB_LAP_LOG:
            if (m_lapLog) m_lapLog->resetToDefaults();
            break;
        case TAB_IDEAL_LAP:
            if (m_idealLap) m_idealLap->resetToDefaults();
            break;
        case TAB_TELEMETRY:
            if (m_telemetry) m_telemetry->resetToDefaults();
            break;
        case TAB_INPUT:
            if (m_input) m_input->resetToDefaults();
            break;
        case TAB_RECORDS:
            if (m_records) m_records->resetToDefaults();
            break;
        case TAB_PITBOARD:
            if (m_pitboard) m_pitboard->resetToDefaults();
            break;
        case TAB_PERFORMANCE:
            if (m_performance) m_performance->resetToDefaults();
            break;
        case TAB_TIMING:
            if (m_timing) m_timing->resetToDefaults();
            break;
        case TAB_GAP_BAR:
            if (m_gapBar) m_gapBar->resetToDefaults();
            break;
        case TAB_WIDGETS:
            // Reset all widgets
            if (m_lap) m_lap->resetToDefaults();
            if (m_position) m_position->resetToDefaults();
            if (m_time) m_time->resetToDefaults();
            if (m_session) m_session->resetToDefaults();
            if (m_speed) m_speed->resetToDefaults();
            if (m_speedo) m_speedo->resetToDefaults();
            if (m_tacho) m_tacho->resetToDefaults();
            if (m_notices) m_notices->resetToDefaults();
            if (m_bars) m_bars->resetToDefaults();
            if (m_version) m_version->resetToDefaults();
            if (m_fuel) m_fuel->resetToDefaults();
            if (m_pointer) m_pointer->resetToDefaults();
            HudManager::getInstance().getSettingsButtonWidget().resetToDefaults();
            break;
        case TAB_RUMBLE:
            // Reset rumble configuration and RumbleHud to defaults
            XInputReader::getInstance().getRumbleConfig().resetToDefaults();
            if (m_rumble) m_rumble->resetToDefaults();
            break;
        case TAB_HOTKEYS:
            // Reset hotkey bindings to defaults
            HotkeyManager::getInstance().resetToDefaults();
            break;
        case TAB_RIDERS:
            // Clear all tracked riders
            TrackedRidersManager::getInstance().clearAll();
            break;
        default:
            DEBUG_WARN_F("Unknown tab index for reset: %d", m_activeTab);
            break;
    }

    // Update settings display
    rebuildRenderData();

    // Save settings after reset
    SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());
}

void SettingsHud::resetCurrentProfile() {
    // Reset only Elements (HUDs and Widgets) for the current profile
    // Global settings (ColorConfig, Rumble, UpdateChecker) are NOT reset here

    // Reset all HUDs to their constructor defaults
    if (m_idealLap) m_idealLap->resetToDefaults();
    if (m_lapLog) m_lapLog->resetToDefaults();
    if (m_standings) m_standings->resetToDefaults();
    if (m_performance) m_performance->resetToDefaults();
    if (m_telemetry) m_telemetry->resetToDefaults();
    if (m_input) m_input->resetToDefaults();
    if (m_mapHud) m_mapHud->resetToDefaults();
    if (m_radarHud) m_radarHud->resetToDefaults();
    if (m_pitboard) m_pitboard->resetToDefaults();
    if (m_records) m_records->resetToDefaults();
    if (m_timing) m_timing->resetToDefaults();
    if (m_gapBar) m_gapBar->resetToDefaults();

    // Reset all widgets to their constructor defaults
    if (m_lap) m_lap->resetToDefaults();
    if (m_position) m_position->resetToDefaults();
    if (m_time) m_time->resetToDefaults();
    if (m_session) m_session->resetToDefaults();
    if (m_speed) m_speed->resetToDefaults();
    if (m_speedo) m_speedo->resetToDefaults();
    if (m_tacho) m_tacho->resetToDefaults();
    if (m_notices) m_notices->resetToDefaults();
    if (m_bars) m_bars->resetToDefaults();
    if (m_version) m_version->resetToDefaults();
    if (m_fuel) m_fuel->resetToDefaults();
    if (m_pointer) m_pointer->resetToDefaults();

    // Reset settings button (managed by HudManager)
    HudManager::getInstance().getSettingsButtonWidget().resetToDefaults();

    // Reset RumbleHud position only (not RumbleConfig which is global)
    if (m_rumble) m_rumble->resetToDefaults();

    // Note: ColorConfig, RumbleConfig, and UpdateChecker are global settings
    // They are NOT reset when resetting a single profile

    // Update settings display
    rebuildRenderData();

    // Save settings for current profile only
    SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());
}

void SettingsHud::handleCheckboxClick(const ClickRegion& region) {
    if (!region.isRequired) {
        auto* bitfield = std::get_if<uint32_t*>(&region.targetPointer);
        if (bitfield && *bitfield && region.targetHud) {
            uint32_t oldValue = **bitfield;
            **bitfield ^= region.flagBit;  // XOR to toggle
            uint32_t newValue = **bitfield;
            region.targetHud->setDataDirty();
            rebuildRenderData();
            DEBUG_INFO_F("Data checkbox toggled: bit 0x%X, bitfield 0x%X -> 0x%X",
                region.flagBit, oldValue, newValue);
        }
    }
}

void SettingsHud::handleGapModeClick(const ClickRegion& region, bool forward) {
    auto* gapMode = std::get_if<StandingsHud::GapMode*>(&region.targetPointer);
    if (!gapMode || !*gapMode || !region.targetHud) return;

    StandingsHud::GapMode oldMode = **gapMode;
    if (forward) {
        // Cycle forward: OFF -> Player -> ALL -> OFF
        switch (**gapMode) {
            case StandingsHud::GapMode::OFF:
                **gapMode = StandingsHud::GapMode::PLAYER;
                break;
            case StandingsHud::GapMode::PLAYER:
                **gapMode = StandingsHud::GapMode::ALL;
                break;
            case StandingsHud::GapMode::ALL:
                **gapMode = StandingsHud::GapMode::OFF;
                break;
            default:
                **gapMode = StandingsHud::GapMode::OFF;
                break;
        }
    } else {
        // Cycle backward: OFF <- Player <- ALL <- OFF
        switch (**gapMode) {
            case StandingsHud::GapMode::OFF:
                **gapMode = StandingsHud::GapMode::ALL;
                break;
            case StandingsHud::GapMode::PLAYER:
                **gapMode = StandingsHud::GapMode::OFF;
                break;
            case StandingsHud::GapMode::ALL:
                **gapMode = StandingsHud::GapMode::PLAYER;
                break;
            default:
                **gapMode = StandingsHud::GapMode::OFF;
                break;
        }
    }
    region.targetHud->setDataDirty();
    rebuildRenderData();
    DEBUG_INFO_F("Gap mode cycled: %d -> %d", static_cast<int>(oldMode), static_cast<int>(**gapMode));
}

void SettingsHud::handleGapIndicatorClick(const ClickRegion& region, bool forward) {
    auto* gapIndicatorMode = std::get_if<StandingsHud::GapIndicatorMode*>(&region.targetPointer);
    if (!gapIndicatorMode || !*gapIndicatorMode || !region.targetHud) return;

    StandingsHud::GapIndicatorMode oldMode = **gapIndicatorMode;
    if (forward) {
        // Cycle forward: OFF -> OFFICIAL -> LIVE -> BOTH -> OFF
        switch (**gapIndicatorMode) {
            case StandingsHud::GapIndicatorMode::OFF:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFFICIAL;
                break;
            case StandingsHud::GapIndicatorMode::OFFICIAL:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::LIVE;
                break;
            case StandingsHud::GapIndicatorMode::LIVE:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::BOTH;
                break;
            case StandingsHud::GapIndicatorMode::BOTH:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                break;
            default:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                break;
        }
    } else {
        // Cycle backward: OFF <- OFFICIAL <- LIVE <- BOTH <- OFF
        switch (**gapIndicatorMode) {
            case StandingsHud::GapIndicatorMode::OFF:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::BOTH;
                break;
            case StandingsHud::GapIndicatorMode::OFFICIAL:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                break;
            case StandingsHud::GapIndicatorMode::LIVE:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFFICIAL;
                break;
            case StandingsHud::GapIndicatorMode::BOTH:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::LIVE;
                break;
            default:
                **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                break;
        }
    }
    region.targetHud->setDataDirty();
    rebuildRenderData();
    DEBUG_INFO_F("Gap indicator mode cycled: %d -> %d", static_cast<int>(oldMode), static_cast<int>(**gapIndicatorMode));
}

void SettingsHud::handleGapReferenceClick(const ClickRegion& region, bool forward) {
    auto* gapReferenceMode = std::get_if<StandingsHud::GapReferenceMode*>(&region.targetPointer);
    if (!gapReferenceMode || !*gapReferenceMode || !region.targetHud) return;

    StandingsHud::GapReferenceMode oldMode = **gapReferenceMode;
    // Toggle between LEADER and PLAYER (only two modes)
    if (**gapReferenceMode == StandingsHud::GapReferenceMode::LEADER) {
        **gapReferenceMode = StandingsHud::GapReferenceMode::PLAYER;
    } else {
        **gapReferenceMode = StandingsHud::GapReferenceMode::LEADER;
    }

    region.targetHud->setDataDirty();
    rebuildRenderData();
    DEBUG_INFO_F("Gap reference mode cycled: %d -> %d", static_cast<int>(oldMode), static_cast<int>(**gapReferenceMode));
}

void SettingsHud::handleHudToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;

    region.targetHud->setVisible(!region.targetHud->isVisible());
    rebuildRenderData();
    DEBUG_INFO_F("HUD visibility toggled: %s", region.targetHud->isVisible() ? "visible" : "hidden");
}

void SettingsHud::handleTitleToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;

    region.targetHud->setShowTitle(!region.targetHud->getShowTitle());
    rebuildRenderData();
    DEBUG_INFO_F("HUD title toggled: %s", region.targetHud->getShowTitle() ? "shown" : "hidden");
}

void SettingsHud::handleOpacityClick(const ClickRegion& region, bool increase) {
    if (!region.targetHud) return;

    float currentOpacity = region.targetHud->getBackgroundOpacity();
    float newOpacity = std::round((currentOpacity + (increase ? 0.10f : -0.10f)) * 10.0f) / 10.0f;
    newOpacity = std::max(0.0f, std::min(1.0f, newOpacity));
    region.targetHud->setBackgroundOpacity(newOpacity);
    rebuildRenderData();
    DEBUG_INFO_F("HUD background opacity %s to %d%%",
        increase ? "increased" : "decreased", static_cast<int>(std::round(newOpacity * 100.0f)));
}

void SettingsHud::handleScaleClick(const ClickRegion& region, bool increase) {
    if (!region.targetHud) return;

    float currentScale = region.targetHud->getScale();
    float newScale = std::round((currentScale + (increase ? 0.1f : -0.1f)) * 10.0f) / 10.0f;
    newScale = std::max(0.5f, std::min(3.0f, newScale));
    region.targetHud->setScale(newScale);
    rebuildRenderData();
    DEBUG_INFO_F("HUD scale %s to %.2f", increase ? "increased" : "decreased", newScale);
}

void SettingsHud::handleRowCountClick(const ClickRegion& region, bool increase) {
    StandingsHud* standings = dynamic_cast<StandingsHud*>(region.targetHud);
    if (standings) {
        int newRowCount = standings->m_displayRowCount + (increase ? 2 : -2);
        if (newRowCount > StandingsHud::MAX_ROW_COUNT) newRowCount = StandingsHud::MAX_ROW_COUNT;
        if (newRowCount < StandingsHud::MIN_ROW_COUNT) newRowCount = StandingsHud::MIN_ROW_COUNT;
        standings->m_displayRowCount = newRowCount;
        standings->setDataDirty();
        rebuildRenderData();
        DEBUG_INFO_F("StandingsHud row count %s to %d", increase ? "increased" : "decreased", newRowCount);
    }
}

void SettingsHud::handleLapLogRowCountClick(const ClickRegion& region, bool increase) {
    LapLogHud* lapLog = dynamic_cast<LapLogHud*>(region.targetHud);
    if (lapLog) {
        int newRowCount = lapLog->m_maxDisplayLaps + (increase ? 1 : -1);
        if (newRowCount > LapLogHud::MAX_DISPLAY_LAPS) newRowCount = LapLogHud::MAX_DISPLAY_LAPS;
        if (newRowCount < LapLogHud::MIN_DISPLAY_LAPS) newRowCount = LapLogHud::MIN_DISPLAY_LAPS;
        lapLog->m_maxDisplayLaps = newRowCount;
        lapLog->setDataDirty();
        rebuildRenderData();
        DEBUG_INFO_F("LapLogHud row count %s to %d", increase ? "increased" : "decreased", newRowCount);
    }
}

void SettingsHud::handleMapRotationClick(const ClickRegion& region) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        bool newRotate = !mapHud->getRotateToPlayer();
        mapHud->setRotateToPlayer(newRotate);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud rotation mode %s", newRotate ? "enabled" : "disabled");
    }
}

void SettingsHud::handleMapOutlineClick(const ClickRegion& region) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        bool newOutline = !mapHud->getShowOutline();
        mapHud->setShowOutline(newOutline);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud outline %s", newOutline ? "enabled" : "disabled");
    }
}

void SettingsHud::handleMapColorizeClick(const ClickRegion& region, bool forward) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        auto newMode = cycleEnum(mapHud->getRiderColorMode(), 3, forward);
        mapHud->setRiderColorMode(newMode);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud rider color mode set to %s", getRiderColorModeName(static_cast<int>(newMode)));
    }
}

void SettingsHud::handleMapTrackWidthClick(const ClickRegion& region, bool increase) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        // Adjust track width scale in 10% increments (0.1)
        float newScale = mapHud->getTrackWidthScale() + (increase ? 0.1f : -0.1f);
        mapHud->setTrackWidthScale(newScale);  // Setter clamps to valid range
        rebuildRenderData();
        DEBUG_INFO_F("MapHud track width scale %s to %.0f%%", increase ? "increased" : "decreased", mapHud->getTrackWidthScale() * 100.0f);
    }
}

void SettingsHud::handleMapLabelModeClick(const ClickRegion& region, bool forward) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        auto newMode = cycleEnum(mapHud->getLabelMode(), 4, forward);
        mapHud->setLabelMode(newMode);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud label mode set to %s", getLabelModeName(static_cast<int>(newMode)));
    }
}

void SettingsHud::handleMapRangeClick(const ClickRegion& region, bool increase) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (!mapHud) return;

    // Discrete range values: Full (index -1), then 50, 100, 200, 500
    static constexpr float RANGE_VALUES[] = { 50.0f, 100.0f, 200.0f, 500.0f };
    static constexpr int NUM_VALUES = sizeof(RANGE_VALUES) / sizeof(RANGE_VALUES[0]);

    // Find current index (-1 = Full, 0-3 = zoom values)
    int currentIndex = -1;
    if (mapHud->getZoomEnabled()) {
        float currentDist = mapHud->getZoomDistance();
        for (int i = 0; i < NUM_VALUES; ++i) {
            if (std::abs(currentDist - RANGE_VALUES[i]) < 0.5f) {
                currentIndex = i;
                break;
            }
        }
        // If not found in array, default to closest
        if (currentIndex == -1) currentIndex = 0;
    }

    // Calculate new index with wrapping
    int newIndex;
    if (increase) {
        // Full → 10 → 20 → ... → 500 → Full
        newIndex = (currentIndex + 1 + 1) % (NUM_VALUES + 1) - 1;  // +1 to account for Full at -1
    } else {
        // Full → 500 → 200 → ... → 10 → Full
        newIndex = (currentIndex + NUM_VALUES + 1) % (NUM_VALUES + 1) - 1;
    }

    // Apply new value
    if (newIndex == -1) {
        mapHud->setZoomEnabled(false);
        DEBUG_INFO("MapHud range set to Full");
    } else {
        mapHud->setZoomEnabled(true);
        mapHud->setZoomDistance(RANGE_VALUES[newIndex]);
        DEBUG_INFO_F("MapHud range set to %.0fm", RANGE_VALUES[newIndex]);
    }
    rebuildRenderData();
}

void SettingsHud::handleMapRiderShapeClick(const ClickRegion& region, bool forward) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        // Map has 11 shapes: OFF(0), ARROWUP(1)...VINYL(10)
        constexpr int NUM_SHAPES = 11;
        int current = static_cast<int>(mapHud->getRiderShape());
        int next = forward ? (current + 1) % NUM_SHAPES : (current - 1 + NUM_SHAPES) % NUM_SHAPES;
        mapHud->setRiderShape(static_cast<MapHud::RiderShape>(next));
        rebuildRenderData();
        DEBUG_INFO_F("MapHud rider shape changed to %d", next);
    }
}

void SettingsHud::handleMapMarkerScaleClick(const ClickRegion& region, bool increase) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        // Adjust marker scale in 10% increments (0.1)
        float newScale = mapHud->getMarkerScale() + (increase ? 0.1f : -0.1f);
        mapHud->setMarkerScale(newScale);  // Setter clamps to valid range
        rebuildRenderData();
        DEBUG_INFO_F("MapHud marker scale %s to %.0f%%", increase ? "increased" : "decreased", mapHud->getMarkerScale() * 100.0f);
    }
}

void SettingsHud::handleRadarRangeClick(const ClickRegion& region, bool increase) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        float newRange = radarHud->getRadarRange() + (increase ? RadarHud::RADAR_RANGE_STEP : -RadarHud::RADAR_RANGE_STEP);
        radarHud->setRadarRange(newRange);
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud range %s to %.0fm", increase ? "increased" : "decreased", newRange);
    }
}

void SettingsHud::handleRadarColorizeClick(const ClickRegion& region, bool forward) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        auto newMode = cycleEnum(radarHud->getRiderColorMode(), 3, forward);
        radarHud->setRiderColorMode(newMode);
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud rider color mode set to %s", getRiderColorModeName(static_cast<int>(newMode)));
    }
}

void SettingsHud::handleRadarAlertDistanceClick(const ClickRegion& region, bool increase) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        float newDist = radarHud->getAlertDistance() + (increase ? RadarHud::ALERT_DISTANCE_STEP : -RadarHud::ALERT_DISTANCE_STEP);
        radarHud->setAlertDistance(newDist);
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud alert distance %s to %.0fm", increase ? "increased" : "decreased", newDist);
    }
}

void SettingsHud::handleRadarLabelModeClick(const ClickRegion& region, bool forward) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        auto newMode = cycleEnum(radarHud->getLabelMode(), 4, forward);
        radarHud->setLabelMode(newMode);
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud label mode set to %s", getLabelModeName(static_cast<int>(newMode)));
    }
}

void SettingsHud::handleRadarRiderShapeClick(const ClickRegion& region, bool forward) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        // Radar has 10 shapes: ARROWUP(0)...VINYL(9), no OFF option
        constexpr int NUM_SHAPES = 10;
        int current = static_cast<int>(radarHud->getRiderShape());
        int next = forward ? (current + 1) % NUM_SHAPES : (current - 1 + NUM_SHAPES) % NUM_SHAPES;
        radarHud->setRiderShape(static_cast<RadarHud::RiderShape>(next));
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud rider shape changed to %d", next);
    }
}

void SettingsHud::handleRadarMarkerScaleClick(const ClickRegion& region, bool increase) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        // Adjust marker scale in 10% increments (0.1)
        float newScale = radarHud->getMarkerScale() + (increase ? 0.1f : -0.1f);
        radarHud->setMarkerScale(newScale);  // Setter clamps to valid range
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud marker scale %s to %.0f%%", increase ? "increased" : "decreased", radarHud->getMarkerScale() * 100.0f);
    }
}

void SettingsHud::handleDisplayModeClick(const ClickRegion& region, bool increase) {
    auto* displayMode = std::get_if<uint8_t*>(&region.targetPointer);
    if (!displayMode || !*displayMode || !region.targetHud) return;

    // DisplayMode enum values are the same for PerformanceHud and TelemetryHud (0=Graphs, 1=Values, 2=Both)
    uint8_t currentMode = **displayMode;
    uint8_t newMode;

    if (increase) {
        // Cycle forward: GRAPHS(0) -> VALUES(1) -> BOTH(2) -> GRAPHS(0)
        switch (currentMode) {
            case 0: newMode = 1; break;  // GRAPHS -> VALUES
            case 1: newMode = 2; break;  // VALUES -> BOTH
            case 2: newMode = 0; break;  // BOTH -> GRAPHS
            default: newMode = 2; break; // Default to BOTH
        }
    } else {
        // Cycle backward: GRAPHS(0) -> BOTH(2) -> VALUES(1) -> GRAPHS(0)
        switch (currentMode) {
            case 0: newMode = 2; break;  // GRAPHS -> BOTH
            case 1: newMode = 0; break;  // VALUES -> GRAPHS
            case 2: newMode = 1; break;  // BOTH -> VALUES
            default: newMode = 2; break; // Default to BOTH
        }
    }

    **displayMode = newMode;
    region.targetHud->setDataDirty();
    rebuildRenderData();

    const char* modeNames[] = {"Graphs", "Numbers", "Both"};
    DEBUG_INFO_F("Display mode changed to %s", modeNames[newMode]);
}

void SettingsHud::handlePitboardShowModeClick(const ClickRegion& region, bool increase) {
    if (!m_pitboard) return;

    // PitboardHud display modes: MODE_ALWAYS(0), MODE_PIT(1), MODE_SPLITS(2)
    uint8_t currentMode = m_pitboard->m_displayMode;
    uint8_t newMode;

    if (increase) {
        // Cycle forward: ALWAYS(0) -> PIT(1) -> SPLITS(2) -> ALWAYS(0)
        switch (currentMode) {
            case PitboardHud::MODE_ALWAYS: newMode = PitboardHud::MODE_PIT; break;
            case PitboardHud::MODE_PIT:    newMode = PitboardHud::MODE_SPLITS; break;
            case PitboardHud::MODE_SPLITS: newMode = PitboardHud::MODE_ALWAYS; break;
            default:                       newMode = PitboardHud::MODE_ALWAYS; break;
        }
    } else {
        // Cycle backward: ALWAYS(0) -> SPLITS(2) -> PIT(1) -> ALWAYS(0)
        switch (currentMode) {
            case PitboardHud::MODE_ALWAYS: newMode = PitboardHud::MODE_SPLITS; break;
            case PitboardHud::MODE_PIT:    newMode = PitboardHud::MODE_ALWAYS; break;
            case PitboardHud::MODE_SPLITS: newMode = PitboardHud::MODE_PIT; break;
            default:                       newMode = PitboardHud::MODE_ALWAYS; break;
        }
    }

    m_pitboard->m_displayMode = newMode;
    m_pitboard->setDataDirty();
    rebuildRenderData();

    const char* modeNames[] = {"Always", "Pit", "Splits"};
    DEBUG_INFO_F("Pitboard show mode changed to %s", modeNames[newMode]);
}

void SettingsHud::handleColorCycleClick(const ClickRegion& region, bool forward) {
    auto* colorSlotPtr = std::get_if<ColorSlot>(&region.targetPointer);
    if (!colorSlotPtr) return;

    ColorSlot slot = *colorSlotPtr;
    ColorConfig& colorConfig = ColorConfig::getInstance();

    colorConfig.cycleColor(slot, forward);

    // Mark all HUDs as dirty so they rebuild with new colors immediately
    HudManager::getInstance().markAllHudsDirty();

    rebuildRenderData();
}

void SettingsHud::handleTabClick(const ClickRegion& region) {
    m_activeTab = region.tabIndex;
    rebuildRenderData();
    DEBUG_INFO_F("Switched to tab %d", m_activeTab);
}

void SettingsHud::handleCloseButtonClick() {
    hide();
    DEBUG_INFO("Settings menu closed via close button");
}

bool SettingsHud::isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const {
    // Apply offset to rectangle position for dragging support
    float offsetRectX = rectX;
    float offsetRectY = rectY;
    applyOffset(offsetRectX, offsetRectY);

    return x >= offsetRectX && x <= (offsetRectX + width) &&
           y >= offsetRectY && y <= (offsetRectY + height);
}
