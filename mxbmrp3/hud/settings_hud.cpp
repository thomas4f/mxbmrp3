// ============================================================================
// hud/settings_hud.cpp
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#include "settings_hud.h"
#include "settings/settings_layout.h"
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
#include "../core/update_downloader.h"
#include "../core/hotkey_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include "../core/font_config.h"
#include "../core/ui_config.h"
#include "../core/plugin_data.h"
#include "../core/tooltip_manager.h"
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
                         TelemetryHud* telemetry,
                         TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionWidget* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, SpeedoWidget* speedo, TachoWidget* tacho, TimingHud* timing, GapBarHud* gapBar, BarsWidget* bars, VersionWidget* version, NoticesWidget* notices, PitboardHud* pitboard, RecordsHud* records, FuelWidget* fuel, PointerWidget* pointer, RumbleHud* rumble, GamepadWidget* gamepad, LeanWidget* lean)
    : m_idealLap(idealLap),
      m_lapLog(lapLog),
      m_standings(standings),
      m_performance(performance),
      m_telemetry(telemetry),
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
      m_gamepad(gamepad),
      m_lean(lean),
      m_bVisible(false),
      m_copyTargetProfile(-1),  // -1 = no target selected
      m_resetProfileConfirmed(false),
      m_resetAllConfirmed(false),
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
      m_trackedRidersPage(0),
      m_wasUpdateCheckerOnCooldown(false)
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
    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    setBounds(0, 0, 0, 0);  // Clear collision bounds to prevent blocking input
}

void SettingsHud::showUpdatesTab() {
    m_activeTab = TAB_UPDATES;
    setDataDirty();  // Force rebuild even if already visible
    show();
}

void SettingsHud::update() {
    if (!m_bVisible) return;

    // Process dirty flag first (e.g., from showUpdatesTab() or external tab switch)
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
    }

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
            // Update tooltip ID for the hovered region
            if (newHoveredIndex >= 0 && newHoveredIndex < static_cast<int>(m_clickRegions.size())) {
                const ClickRegion& region = m_clickRegions[newHoveredIndex];
                // Use tooltipId from region if set (Phase 3), otherwise fall back to type-based lookup
                if (!region.tooltipId.empty()) {
                    m_hoveredTooltipId = region.tooltipId;
                } else {
                    const char* tooltipId = getTooltipIdForRegion(region.type, m_activeTab);
                    m_hoveredTooltipId = tooltipId ? tooltipId : "";
                }
            } else {
                m_hoveredTooltipId.clear();
            }
            rebuildRenderData();  // Rebuild to update button backgrounds and tooltip
        }

        // For hotkeys tab, track row and column hover
        if (m_activeTab == TAB_HOTKEYS && m_hotkeyRowHeight > 0.0f) {
            int newHoveredRow = -1;
            HotkeyColumn newHoveredColumn = HotkeyColumn::NONE;

            // Apply offset to stored coordinates for comparison with cursor
            float contentStartY = m_hotkeyContentStartY + m_fOffsetY;
            float keyboardX = m_hotkeyKeyboardX + m_fOffsetX;
            float controllerX = m_hotkeyControllerX + m_fOffsetX;

            if (cursor.y >= contentStartY) {
                // Calculate which row the mouse is over
                float relativeY = cursor.y - contentStartY;

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
                    float kbFieldEnd = keyboardX + m_hotkeyFieldCharWidth * (kbFieldWidth + 2);
                    float ctrlFieldEnd = controllerX + m_hotkeyFieldCharWidth * (ctrlFieldWidth + 2);

                    if (cursor.x >= keyboardX && cursor.x < kbFieldEnd) {
                        newHoveredColumn = HotkeyColumn::KEYBOARD;
                    } else if (cursor.x >= controllerX && cursor.x < ctrlFieldEnd) {
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

            // Apply offset to stored coordinates for comparison with cursor
            float ridersStartY = m_trackedRidersStartY + m_fOffsetY;
            float ridersStartX = m_trackedRidersStartX + m_fOffsetX;

            if (cursor.y >= ridersStartY && cursor.x >= ridersStartX) {
                float relativeY = cursor.y - ridersStartY;
                float relativeX = cursor.x - ridersStartX;

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

    // Check if UpdateChecker cooldown just expired (need to re-enable Check Now button)
    if (m_activeTab == TAB_UPDATES) {
        UpdateChecker& checker = UpdateChecker::getInstance();
        bool wasOnCooldown = m_wasUpdateCheckerOnCooldown;
        bool isOnCooldown = checker.isOnCooldown();
        m_wasUpdateCheckerOnCooldown = isOnCooldown;
        if (wasOnCooldown && !isOnCooldown) {
            setDataDirty();
        }
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

    clearStrings();
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

    // Estimate height - sized to fit Radar tab (most rows: ~22 content + title/close)
    int estimatedRows = 28;
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
        TAB_UPDATES,
        TAB_SECTION_PROFILE,
        TAB_SECTION_ELEMENTS,
        TAB_STANDINGS,
        TAB_MAP,
        TAB_RADAR,
        TAB_LAP_LOG,
        TAB_IDEAL_LAP,
        TAB_TELEMETRY,
        TAB_RECORDS,
        TAB_PITBOARD,
        TAB_TIMING,
        TAB_GAP_BAR,
        TAB_PERFORMANCE,
        TAB_WIDGETS
    };

    for (size_t orderIdx = 0; orderIdx < sizeof(tabDisplayOrder)/sizeof(tabDisplayOrder[0]); orderIdx++) {
        int i = tabDisplayOrder[orderIdx];

        // Skip Records tab if records provider is not available (e.g., GP Bikes)
        if (i == TAB_RECORDS && !m_records) {
            continue;
        }

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
        } else if (i == TAB_UPDATES) {
            isHudEnabled = UpdateChecker::getInstance().isEnabled();
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
        } else if (i == TAB_UPDATES) {
            // Checkbox click region for update checking toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::UPDATE_CHECK_TOGGLE, nullptr
            ));

            // Checkbox text
            const char* checkboxText = isHudEnabled ? "[X]" : "[ ]";
            addString(checkboxText, currentTabX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

            currentTabX += checkboxWidth;
        } else {
            // No checkbox for General tab - just add spacing
            currentTabX += checkboxWidth;
        }

        // Tab click region (for selecting the tab)
        float tabLabelWidth = tabWidth - checkboxWidth;
        size_t tabRegionIndex = m_clickRegions.size();  // Track index for hover check

        // Tab ID for description lookup (lowercase)
        const char* tabId = i == TAB_GENERAL ? "general" :
                           i == TAB_APPEARANCE ? "appearance" :
                           i == TAB_STANDINGS ? "standings" :
                           i == TAB_MAP ? "map" :
                           i == TAB_LAP_LOG ? "lap_log" :
                           i == TAB_IDEAL_LAP ? "ideal_lap" :
                           i == TAB_TELEMETRY ? "telemetry" :
                           i == TAB_PERFORMANCE ? "performance" :
                           i == TAB_PITBOARD ? "pitboard" :
                           i == TAB_RECORDS ? "records" :
                           i == TAB_TIMING ? "timing" :
                           i == TAB_GAP_BAR ? "gap_bar" :
                           i == TAB_WIDGETS ? "widgets" :
                           i == TAB_RUMBLE ? "rumble" :
                           i == TAB_HOTKEYS ? "hotkeys" :
                           i == TAB_RIDERS ? "riders" :
                           i == TAB_UPDATES ? "updates" :
                           "radar";

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
        tabRegion.tooltipId = tabId;  // Show tab description on hover
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

        addString(getTabName(i), currentTabX, tabStartY, Justify::LEFT, Fonts::getNormal(), tabColor, dim.fontSize);

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

    // Create layout context for extracted tabs
    // controlX is where the toggle controls start (labelX + 24 chars for Phase 3 descriptive labels)
    float controlX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(24, dim.fontSize);
    // Compute content area width (from contentAreaStartX to right edge of panel content)
    // This is used for row width calculations to ensure content doesn't extend past the panel
    float contentAreaWidth = (startX + panelWidth - dim.paddingH) - contentAreaStartX;
    SettingsLayoutContext layoutCtx(this, dim, leftColumnX, controlX, rightColumnX,
                                     contentAreaStartX, contentAreaWidth, currentY);

    switch (m_activeTab) {
        case TAB_GENERAL:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabGeneral(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_APPEARANCE:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabAppearance(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_HOTKEYS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabHotkeys(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_STANDINGS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabStandings(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_MAP:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabMap(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_LAP_LOG:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabLapLog(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_IDEAL_LAP:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;  // Sync context cursor
            activeHud = renderTabIdealLap(layoutCtx);
            currentY = layoutCtx.currentY;  // Sync local cursor back
            break;

        case TAB_TELEMETRY:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabTelemetry(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_PERFORMANCE:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabPerformance(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_PITBOARD:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabPitboard(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RECORDS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRecords(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_TIMING:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabTiming(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_GAP_BAR:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabGapBar(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_WIDGETS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabWidgets(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RADAR:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRadar(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RUMBLE:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRumble(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RIDERS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRiders(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_UPDATES:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabUpdates(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        default:
            DEBUG_WARN_F("Invalid tab index: %d, defaulting to TAB_STANDINGS", m_activeTab);
            activeHud = m_standings;
            break;
    }

    currentY += sectionSpacing;

    // Draw hover highlight for TOOLTIP_ROW regions
    if (m_hoveredRegionIndex >= 0 && m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size())) {
        const ClickRegion& hoveredRegion = m_clickRegions[m_hoveredRegionIndex];
        if (hoveredRegion.type == ClickRegion::TOOLTIP_ROW) {
            // Draw highlight behind the hovered row (same opacity as tab hover)
            SPluginQuad_t hoverQuad;
            float hoverX = hoveredRegion.x, hoverY = hoveredRegion.y;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverQuad, hoverX, hoverY, hoveredRegion.width, hoveredRegion.height);
            hoverQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            hoverQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverQuad);
        }
    }

    // Render description or tooltip at the reserved position (replaces each other)
    // Calculate max width for word wrapping (contentAreaWidth - left margin from labels)
    float descTextWidth = layoutCtx.panelWidth - (layoutCtx.labelX - contentAreaStartX);
    int maxCharsPerLine = static_cast<int>(descTextWidth / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize));

    // Helper lambda to render up to 2 lines of word-wrapped text
    auto renderWrappedText = [&](const std::string& text, unsigned long color) {
        float lineY = layoutCtx.tooltipY;
        size_t lineStart = 0;
        int lineCount = 0;
        constexpr int MAX_LINES = 2;

        while (lineStart < text.length() && lineCount < MAX_LINES) {
            std::string wrappedLine;
            size_t lineEnd = lineStart + maxCharsPerLine;

            if (lineEnd >= text.length()) {
                // Last line - use remaining text
                wrappedLine = text.substr(lineStart);
                lineStart = text.length();
            } else {
                // Find last space before lineEnd for word wrap
                size_t lastSpace = text.rfind(' ', lineEnd);
                if (lastSpace != std::string::npos && lastSpace > lineStart) {
                    wrappedLine = text.substr(lineStart, lastSpace - lineStart);
                    lineStart = lastSpace + 1;  // Skip the space
                } else {
                    // No space found - hard break
                    wrappedLine = text.substr(lineStart, maxCharsPerLine);
                    lineStart += maxCharsPerLine;
                }

                // If this is the last line and there's more text, add ellipsis
                if (lineCount == MAX_LINES - 1 && lineStart < text.length()) {
                    if (wrappedLine.length() > 3) {
                        wrappedLine = wrappedLine.substr(0, wrappedLine.length() - 3) + "...";
                    }
                }
            }

            addString(wrappedLine.c_str(), layoutCtx.labelX, lineY, Justify::LEFT,
                Fonts::getNormal(), color, dim.fontSize);
            lineY += dim.lineHeightNormal;
            lineCount++;
        }
    };

    if (!m_hoveredTooltipId.empty()) {
        // Check if hovering a TAB region - show tab description instead of control tooltip
        bool isTabHover = (m_hoveredRegionIndex >= 0 &&
                          m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size()) &&
                          m_clickRegions[m_hoveredRegionIndex].type == ClickRegion::TAB);

        if (isTabHover) {
            // Show tab tooltip for hovered tab
            const char* tabTooltip = TooltipManager::getInstance().getTabTooltip(m_hoveredTooltipId.c_str());
            if (tabTooltip && tabTooltip[0] != '\0') {
                renderWrappedText(std::string(tabTooltip), ColorConfig::getInstance().getMuted());
            }
        } else {
            // Show control tooltip
            const char* tooltipText = TooltipManager::getInstance().getControlTooltip(m_hoveredTooltipId.c_str());
            if (tooltipText && tooltipText[0] != '\0') {
                renderWrappedText(std::string(tooltipText), ColorConfig::getInstance().getMuted());
            }
        }
    } else if (!layoutCtx.currentTabId.empty()) {
        // Show tab tooltip (when not hovering)
        const char* tabTooltip = TooltipManager::getInstance().getTabTooltip(layoutCtx.currentTabId.c_str());
        if (tabTooltip && tabTooltip[0] != '\0') {
            renderWrappedText(std::string(tabTooltip), ColorConfig::getInstance().getMuted());
        }
    }

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

    // Button text - PRIMARY when hovered, ACCENT when not (purple on purple)
    unsigned long closeTextColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getAccent();
    addString("[Close]", closeButtonBottomX, closeButtonBottomY, Justify::CENTER,
        Fonts::getStrong(), closeTextColor, dim.fontSize);

    // [Reset <TabName>] button - bottom left corner
    float resetTabButtonY = closeButtonBottomY;
    char resetTabButtonText[32];
    snprintf(resetTabButtonText, sizeof(resetTabButtonText), "[Reset %s]", getTabName(m_activeTab));
    int resetTabButtonChars = static_cast<int>(strlen(resetTabButtonText));
    float resetTabButtonWidth = PluginUtils::calculateMonospaceTextWidth(resetTabButtonChars, dim.fontSize);
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

    // Reset Tab button text - PRIMARY when hovered, ACCENT when not (purple on purple)
    unsigned long resetTabTextColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getAccent();
    addString(resetTabButtonText, resetTabButtonX + resetTabButtonWidth / 2.0f, resetTabButtonY, Justify::CENTER,
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
            // Query UpdateChecker directly for current status (no duplicate state)
            UpdateChecker::Status status = UpdateChecker::getInstance().getStatus();

            switch (status) {
                case UpdateChecker::Status::IDLE:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    break;
                case UpdateChecker::Status::CHECKING:
                    snprintf(versionStr, sizeof(versionStr), "Checking...");
                    // Keep muted color (same as default)
                    break;
                case UpdateChecker::Status::UP_TO_DATE:
                    snprintf(versionStr, sizeof(versionStr), "v%s up-to-date", PluginConstants::PLUGIN_VERSION);
                    versionColor = ColorConfig::getInstance().getMuted();
                    break;
                case UpdateChecker::Status::UPDATE_AVAILABLE: {
                    // Get latest version directly from UpdateChecker
                    std::string latestVersion = UpdateChecker::getInstance().getLatestVersion();
                    // Show "installed" if downloader completed, otherwise "available"
                    if (UpdateDownloader::getInstance().getState() == UpdateDownloader::State::READY) {
                        snprintf(versionStr, sizeof(versionStr), "%s installed!", latestVersion.c_str());
                    } else {
                        snprintf(versionStr, sizeof(versionStr), "%s available!", latestVersion.c_str());
                    }
                    versionColor = ColorConfig::getInstance().getPositive();
                    break;
                }
                case UpdateChecker::Status::CHECK_FAILED:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    // Silent fail - just show version in muted
                    break;
            }
        }

        // Calculate width for right-alignment
        float versionWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(versionStr)), dim.fontSize);
        float buttonPadding = dim.paddingH * 0.5f;
        float buttonWidth = versionWidth + buttonPadding * 2;
        float versionX = rightEdgeX - buttonWidth;

        // Check if update is available and not yet installed
        bool isUpdateAvailable = (UpdateChecker::getInstance().getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE);
        bool isInstalled = (isUpdateAvailable &&
                           UpdateDownloader::getInstance().getState() == UpdateDownloader::State::READY);

        // Always add click region for easter egg (and update navigation when available)
        size_t regionIndex = m_clickRegions.size();
        ClickRegion versionRegion;
        versionRegion.type = ClickRegion::VERSION_CLICK;
        versionRegion.y = versionY;
        versionRegion.height = dim.lineHeightNormal;

        // If update is available (not yet installed), show as clickable button
        if (isUpdateAvailable && !isInstalled) {
            versionRegion.x = versionX;
            versionRegion.width = buttonWidth;
            m_clickRegions.push_back(versionRegion);

            bool isHovered = m_hoveredRegionIndex == static_cast<int>(regionIndex);

            // Button background
            SPluginQuad_t bgQuad;
            float bgX = versionX, bgY = versionY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, buttonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = isHovered ? ColorConfig::getInstance().getPositive()
                : PluginUtils::applyOpacity(ColorConfig::getInstance().getPositive(), 0.5f);
            m_quads.push_back(bgQuad);

            // Text color: positive (green) when unhovered for contrast, primary when hovered
            versionColor = isHovered ? ColorConfig::getInstance().getPrimary()
                : ColorConfig::getInstance().getPositive();

            // Draw centered in button
            float textX = versionX + buttonWidth * 0.5f;
            addString(versionStr, textX, versionY, Justify::CENTER,
                Fonts::getNormal(), versionColor, dim.fontSize);
        } else {
            // Regular text (not a button) - right aligned, but still clickable for easter egg
            float textX = rightEdgeX - versionWidth;
            versionRegion.x = textX;
            versionRegion.width = versionWidth;
            m_clickRegions.push_back(versionRegion);

            addString(versionStr, textX, versionY, Justify::LEFT,
                Fonts::getNormal(), versionColor, dim.fontSize);
        }
    }
}

void SettingsHud::handleClick(float mouseX, float mouseY) {
    // Check each clickable region
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // Skip TOOLTIP_ROW regions - they're hover-only for tooltip display
            if (region.type == ClickRegion::TOOLTIP_ROW) continue;

            // Try tab-specific handlers first (implemented in separate files)
            bool handled = false;
            switch (m_activeTab) {
                case TAB_MAP:        handled = handleClickTabMap(region); break;
                case TAB_RADAR:      handled = handleClickTabRadar(region); break;
                case TAB_TIMING:     handled = handleClickTabTiming(region); break;
                case TAB_GAP_BAR:    handled = handleClickTabGapBar(region); break;
                case TAB_STANDINGS:  handled = handleClickTabStandings(region); break;
                case TAB_RUMBLE:     handled = handleClickTabRumble(region); break;
                case TAB_APPEARANCE: handled = handleClickTabAppearance(region); break;
                case TAB_GENERAL:    handled = handleClickTabGeneral(region); break;
                case TAB_HOTKEYS:    handled = handleClickTabHotkeys(region); break;
                case TAB_RIDERS:     handled = handleClickTabRiders(region); break;
                case TAB_RECORDS:    handled = handleClickTabRecords(region); break;
                case TAB_PITBOARD:   handled = handleClickTabPitboard(region); break;
                case TAB_LAP_LOG:    handled = handleClickTabLapLog(region); break;
                case TAB_UPDATES:    handled = handleClickTabUpdates(region); break;
                default: break;
            }

            if (handled) {
                // Tab handler processed the click - save and return
                SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());
                return;
            }

            // Fall through to common handlers for shared controls
            switch (region.type) {
                // ============================================
                // Common handlers (used across multiple tabs)
                // Tab-specific handlers are in settings_tab_*.cpp files
                // ============================================

                case ClickRegion::CHECKBOX:
                    handleCheckboxClick(region);
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
                case ClickRegion::UPDATE_CHECK_TOGGLE:
                    {
                        UpdateChecker& checker = UpdateChecker::getInstance();
                        bool newState = !checker.isEnabled();
                        checker.setEnabled(newState);
                        if (newState && !checker.isChecking()) {
                            // Trigger an update check when enabled
                            checker.setCompletionCallback([this]() {
                                setDataDirty();
                            });
                            checker.checkForUpdates();
                        }
                        rebuildRenderData();
                        DEBUG_INFO_F("Update checking toggle: %s", newState ? "enabled" : "disabled");
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
                // Note: ROW_COUNT, LAP_LOG_ROW_COUNT, MAP_*, RADAR_* handlers moved to tab files

                case ClickRegion::DISPLAY_MODE_UP:
                    handleDisplayModeClick(region, true);
                    break;
                case ClickRegion::DISPLAY_MODE_DOWN:
                    handleDisplayModeClick(region, false);
                    break;
                // Profile cycle controls are in sidebar, must work from ALL tabs
                case ClickRegion::PROFILE_CYCLE_UP:
                    {
                        ProfileType nextProfile = ProfileManager::getNextProfile(
                            ProfileManager::getInstance().getActiveProfile());
                        SettingsManager::getInstance().switchProfile(HudManager::getInstance(), nextProfile);
                        rebuildRenderData();
                    }
                    return;  // Don't save - switchProfile already saves
                case ClickRegion::PROFILE_CYCLE_DOWN:
                    {
                        ProfileType prevProfile = ProfileManager::getPreviousProfile(
                            ProfileManager::getInstance().getActiveProfile());
                        SettingsManager::getInstance().switchProfile(HudManager::getInstance(), prevProfile);
                        rebuildRenderData();
                    }
                    return;  // Don't save - switchProfile already saves
                // Note: Tab-specific handlers moved to settings_tab_*.cpp files:
                // RECORDS_COUNT, PITBOARD_SHOW_MODE, TIMING_*, GAPBAR_*,
                // COLOR_CYCLE_*, FONT_CATEGORY_*, SPEED_UNIT, FUEL_UNIT,
                // GRID_SNAP, UPDATE_CHECK, COPY_*, RESET_*
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
                // Note: Tab-specific handlers moved to settings_tab_*.cpp files:
                // RUMBLE_*, HOTKEY_*, RIDER_*, pagination controls

                case ClickRegion::VERSION_CLICK:
                    {
                        // If update is available, navigate to Updates tab
                        if (UpdateChecker::getInstance().getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE) {
                            m_activeTab = TAB_UPDATES;
                            rebuildRenderData();
                            return;  // Don't process easter egg
                        }

                        // Otherwise, easter egg logic
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
    if (m_gamepad) m_gamepad->resetToDefaults();
    if (m_lean) m_lean->resetToDefaults();
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

    // Reset master toggles
    HudManager::getInstance().setWidgetsEnabled(true);

    // Reset advanced settings (power-user options)
    if (m_mapHud) m_mapHud->setPixelSpacing(MapHud::DEFAULT_PIXEL_SPACING);

    // Reset update checker to default (off)
    UpdateChecker::getInstance().setEnabled(false);

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
            UiConfig::getInstance().setGridSnapping(true);  // Reset grid snap
            UiConfig::getInstance().setScreenClamping(true);  // Reset screen clamp
            // Reset update checker
            UpdateChecker::getInstance().setEnabled(false);
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
            if (m_gamepad) m_gamepad->resetToDefaults();
            if (m_lean) m_lean->resetToDefaults();
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
    if (m_gamepad) m_gamepad->resetToDefaults();
    if (m_lean) m_lean->resetToDefaults();
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
            // For multi-bit flags (like COL_SECTORS), use set/clear instead of XOR
            // If all bits are set, clear them; otherwise set all
            if ((oldValue & region.flagBit) == region.flagBit) {
                **bitfield &= ~region.flagBit;  // Clear all flag bits
            } else {
                **bitfield |= region.flagBit;   // Set all flag bits
            }
            uint32_t newValue = **bitfield;
            region.targetHud->setDataDirty();
            rebuildRenderData();
            DEBUG_INFO_F("Data checkbox toggled: bit 0x%X, bitfield 0x%X -> 0x%X",
                region.flagBit, oldValue, newValue);
        }
    }
}

// Note: handleGapModeClick, handleGapIndicatorClick, handleGapReferenceClick
// moved to settings_tab_standings.cpp

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

// Note: handleRowCountClick, handleLapLogRowCountClick, handleMap*, handleRadar*
// moved to respective tab files (settings_tab_standings.cpp, settings_tab_lap_log.cpp,
// settings_tab_map.cpp, settings_tab_radar.cpp)

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

// Note: handlePitboardShowModeClick moved to settings_tab_pitboard.cpp
// Note: handleColorCycleClick moved to settings_tab_appearance.cpp

void SettingsHud::handleTabClick(const ClickRegion& region) {
    m_activeTab = region.tabIndex;
    rebuildRenderData();
    DEBUG_INFO_F("Switched to tab %d", m_activeTab);
}

void SettingsHud::handleCloseButtonClick() {
    hide();
    DEBUG_INFO("Settings menu closed via close button");
}

const char* SettingsHud::getTabName(int tabIndex) const {
    switch (tabIndex) {
        case TAB_GENERAL:     return "General";
        case TAB_APPEARANCE:  return "Appearance";
        case TAB_STANDINGS:   return "Standings";
        case TAB_MAP:         return "Map";
        case TAB_LAP_LOG:     return "Lap Log";
        case TAB_IDEAL_LAP:   return "Ideal Lap";
        case TAB_TELEMETRY:   return "Telemetry";
        case TAB_PERFORMANCE: return "Performance";
        case TAB_PITBOARD:    return "Pitboard";
        case TAB_RECORDS:     return "Records";
        case TAB_TIMING:      return "Timing";
        case TAB_GAP_BAR:     return "Gap Bar";
        case TAB_WIDGETS:     return "Widgets";
        case TAB_RUMBLE:      return "Rumble";
        case TAB_HOTKEYS:     return "Hotkeys";
        case TAB_RIDERS:      return "Riders";
        case TAB_UPDATES:     return "Updates";
        case TAB_RADAR:       return "Radar";
        default:              return "Unknown";
    }
}

bool SettingsHud::isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const {
    // Apply offset to rectangle position for dragging support
    float offsetRectX = rectX;
    float offsetRectY = rectY;
    applyOffset(offsetRectX, offsetRectY);

    return x >= offsetRectX && x <= (offsetRectX + width) &&
           y >= offsetRectY && y <= (offsetRectY + height);
}

const char* SettingsHud::getTooltipIdForRegion(ClickRegion::Type type, int activeTab) {
    // Map click region types to tooltip IDs
    // Common controls (used across all tabs)
    switch (type) {
        case ClickRegion::HUD_TOGGLE:
            return "common.visible";
        case ClickRegion::TITLE_TOGGLE:
            return "common.title";
        case ClickRegion::TEXTURE_VARIANT_UP:
        case ClickRegion::TEXTURE_VARIANT_DOWN:
            return "common.texture";
        case ClickRegion::BACKGROUND_OPACITY_UP:
        case ClickRegion::BACKGROUND_OPACITY_DOWN:
            return "common.opacity";
        case ClickRegion::SCALE_UP:
        case ClickRegion::SCALE_DOWN:
            return "common.scale";
        default:
            break;
    }

    // Tab-specific controls
    switch (activeTab) {
        case TAB_STANDINGS:
            switch (type) {
                case ClickRegion::ROW_COUNT_UP:
                case ClickRegion::ROW_COUNT_DOWN:
                    return "standings.rows";
                case ClickRegion::GAP_MODE_UP:
                case ClickRegion::GAP_MODE_DOWN:
                    return "standings.gap_mode";
                case ClickRegion::GAP_INDICATOR_UP:
                case ClickRegion::GAP_INDICATOR_DOWN:
                    return "standings.gap_indicator";
                case ClickRegion::GAP_REFERENCE_UP:
                case ClickRegion::GAP_REFERENCE_DOWN:
                    return "standings.gap_reference";
                default:
                    break;
            }
            break;

        case TAB_MAP:
            switch (type) {
                case ClickRegion::MAP_ROTATION_TOGGLE:
                    return "map.rotation";
                case ClickRegion::MAP_OUTLINE_TOGGLE:
                    return "map.outline";
                case ClickRegion::MAP_COLORIZE_UP:
                case ClickRegion::MAP_COLORIZE_DOWN:
                    return "map.colorize";
                case ClickRegion::MAP_TRACK_WIDTH_UP:
                case ClickRegion::MAP_TRACK_WIDTH_DOWN:
                    return "map.track_width";
                case ClickRegion::MAP_LABEL_MODE_UP:
                case ClickRegion::MAP_LABEL_MODE_DOWN:
                    return "map.labels";
                case ClickRegion::MAP_RANGE_UP:
                case ClickRegion::MAP_RANGE_DOWN:
                    return "map.range";
                case ClickRegion::MAP_RIDER_SHAPE_UP:
                case ClickRegion::MAP_RIDER_SHAPE_DOWN:
                    return "map.rider_shape";
                case ClickRegion::MAP_MARKER_SCALE_UP:
                case ClickRegion::MAP_MARKER_SCALE_DOWN:
                    return "map.marker_scale";
                default:
                    break;
            }
            break;

        case TAB_RADAR:
            switch (type) {
                case ClickRegion::RADAR_RANGE_UP:
                case ClickRegion::RADAR_RANGE_DOWN:
                    return "radar.range";
                case ClickRegion::RADAR_COLORIZE_UP:
                case ClickRegion::RADAR_COLORIZE_DOWN:
                    return "radar.colorize";
                case ClickRegion::RADAR_PLAYER_ARROW_TOGGLE:
                    return "radar.player_arrow";
                case ClickRegion::RADAR_ALERT_DISTANCE_UP:
                case ClickRegion::RADAR_ALERT_DISTANCE_DOWN:
                    return "radar.alert_distance";
                case ClickRegion::RADAR_LABEL_MODE_UP:
                case ClickRegion::RADAR_LABEL_MODE_DOWN:
                    return "radar.labels";
                case ClickRegion::RADAR_MODE_UP:
                case ClickRegion::RADAR_MODE_DOWN:
                    return "radar.mode";
                case ClickRegion::RADAR_RIDER_SHAPE_UP:
                case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
                    return "radar.rider_shape";
                case ClickRegion::RADAR_MARKER_SCALE_UP:
                case ClickRegion::RADAR_MARKER_SCALE_DOWN:
                    return "radar.marker_scale";
                default:
                    break;
            }
            break;

        case TAB_LAP_LOG:
            switch (type) {
                case ClickRegion::LAP_LOG_ROW_COUNT_UP:
                case ClickRegion::LAP_LOG_ROW_COUNT_DOWN:
                    return "lap_log.rows";
                case ClickRegion::LAP_LOG_ORDER_UP:
                case ClickRegion::LAP_LOG_ORDER_DOWN:
                    return "lap_log.order";
                case ClickRegion::LAP_LOG_GAP_ROW_TOGGLE:
                    return "lap_log.gap_row";
                default:
                    break;
            }
            break;

        case TAB_TIMING:
            switch (type) {
                case ClickRegion::TIMING_LABEL_TOGGLE:
                    return "timing.label";
                case ClickRegion::TIMING_TIME_TOGGLE:
                    return "timing.time";
                case ClickRegion::TIMING_GAP_UP:
                case ClickRegion::TIMING_GAP_DOWN:
                    return "timing.gap";
                case ClickRegion::TIMING_DISPLAY_MODE_UP:
                case ClickRegion::TIMING_DISPLAY_MODE_DOWN:
                    return "timing.show";
                case ClickRegion::TIMING_DURATION_UP:
                case ClickRegion::TIMING_DURATION_DOWN:
                    return "timing.freeze";
                case ClickRegion::TIMING_REFERENCE_TOGGLE:
                    return "timing.show_reference";
                case ClickRegion::TIMING_LAYOUT_TOGGLE:
                    return "timing.layout";
                case ClickRegion::TIMING_GAP_PB_TOGGLE:
                    return "timing.secondary_pb";
                case ClickRegion::TIMING_GAP_IDEAL_TOGGLE:
                    return "timing.secondary_ideal";
                case ClickRegion::TIMING_GAP_OVERALL_TOGGLE:
                    return "timing.secondary_overall";
                case ClickRegion::TIMING_GAP_ALLTIME_TOGGLE:
                    return "timing.secondary_alltime";
                case ClickRegion::TIMING_GAP_RECORD_TOGGLE:
                    return "timing.secondary_record";
                default:
                    break;
            }
            break;

        case TAB_GAP_BAR:
            switch (type) {
                case ClickRegion::GAPBAR_FREEZE_UP:
                case ClickRegion::GAPBAR_FREEZE_DOWN:
                    return "gap_bar.freeze";
                case ClickRegion::GAPBAR_MARKER_MODE_UP:
                case ClickRegion::GAPBAR_MARKER_MODE_DOWN:
                    return "gap_bar.marker_mode";
                case ClickRegion::GAPBAR_ICON_UP:
                case ClickRegion::GAPBAR_ICON_DOWN:
                    return "gap_bar.icon";
                case ClickRegion::GAPBAR_GAP_TEXT_TOGGLE:
                    return "gap_bar.show_gap";
                case ClickRegion::GAPBAR_GAP_BAR_TOGGLE:
                    return "gap_bar.show_gap_bar";
                case ClickRegion::GAPBAR_RANGE_UP:
                case ClickRegion::GAPBAR_RANGE_DOWN:
                    return "gap_bar.range";
                case ClickRegion::GAPBAR_WIDTH_UP:
                case ClickRegion::GAPBAR_WIDTH_DOWN:
                    return "gap_bar.width";
                case ClickRegion::GAPBAR_MARKER_SCALE_UP:
                case ClickRegion::GAPBAR_MARKER_SCALE_DOWN:
                    return "gap_bar.marker_scale";
                case ClickRegion::GAPBAR_LABEL_MODE_UP:
                case ClickRegion::GAPBAR_LABEL_MODE_DOWN:
                    return "gap_bar.labels";
                default:
                    break;
            }
            break;

        case TAB_RECORDS:
            switch (type) {
                case ClickRegion::RECORDS_COUNT_UP:
                case ClickRegion::RECORDS_COUNT_DOWN:
                    return "records.count";
                default:
                    break;
            }
            break;

        case TAB_PITBOARD:
            switch (type) {
                case ClickRegion::PITBOARD_SHOW_MODE_UP:
                case ClickRegion::PITBOARD_SHOW_MODE_DOWN:
                    return "pitboard.show_mode";
                default:
                    break;
            }
            break;

        case TAB_PERFORMANCE:
        case TAB_TELEMETRY:
            switch (type) {
                case ClickRegion::DISPLAY_MODE_UP:
                case ClickRegion::DISPLAY_MODE_DOWN:
                    return activeTab == TAB_PERFORMANCE ? "performance.display" : "telemetry.display";
                default:
                    break;
            }
            break;

        case TAB_GENERAL:
            switch (type) {
                case ClickRegion::SPEED_UNIT_TOGGLE:
                    return "general.speed_unit";
                case ClickRegion::FUEL_UNIT_TOGGLE:
                    return "general.fuel_unit";
                case ClickRegion::GRID_SNAP_TOGGLE:
                    return "general.grid_snap";
                case ClickRegion::RUMBLE_CONTROLLER_UP:
                case ClickRegion::RUMBLE_CONTROLLER_DOWN:
                    return "general.controller";
                default:
                    break;
            }
            break;

        case TAB_RUMBLE:
            switch (type) {
                case ClickRegion::RUMBLE_TOGGLE:
                    return "rumble.enabled";
                default:
                    break;
            }
            break;

        default:
            break;
    }

    return "";  // No tooltip for this region
}
