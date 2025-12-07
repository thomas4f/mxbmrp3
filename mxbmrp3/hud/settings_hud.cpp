// ============================================================================
// hud/settings_hud.cpp
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#include "settings_hud.h"
#include "telemetry_hud.h"
#include "settings_button_widget.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include <cstring>

using namespace PluginConstants;

SettingsHud::SettingsHud(SessionBestHud* sessionBest, LapLogHud* lapLog,
                         StandingsHud* standings,
                         PerformanceHud* performance,
                         TelemetryHud* telemetry, InputHud* input,
                         TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionWidget* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, SpeedoWidget* speedo, TachoWidget* tacho, TimingWidget* timing, BarsWidget* bars, VersionWidget* version, NoticesWidget* notices, PitboardHud* pitboard, RecordsHud* records, FuelWidget* fuel)
    : m_sessionBest(sessionBest),
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
      m_bars(bars),
      m_version(version),
      m_notices(notices),
      m_pitboard(pitboard),
      m_records(records),
      m_fuel(fuel),
      m_bVisible(false),
      m_cachedWindowWidth(0),
      m_cachedWindowHeight(0),
      m_activeTab(TAB_GENERAL)
{
    DEBUG_INFO("SettingsHud created");
    setDraggable(true);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(60);  // Less text now with tabs
    m_clickRegions.reserve(30);  // Fewer regions per tab

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

    // Handle mouse input
    if (input.getLeftButton().isClicked()) {
        const CursorPosition& cursor = input.getCursorPosition();
        if (cursor.isValid) {
            handleClick(cursor.x, cursor.y);
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
        Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);

    // Cycle control position
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    float controlX = x + PluginUtils::calculateMonospaceTextWidth(12, dims.fontSize);  // Align with other controls
    constexpr int MAX_VALUE_WIDTH = 7;  // "Numbers" is longest

    // Left arrow "<"
    addString("<", controlX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
    addClickRegion(ClickRegion::DISPLAY_MODE_DOWN, controlX, currentY, charWidth * 2, dims.lineHeightNormal,
                   targetHud, nullptr, displayMode, 0, false, 0);
    controlX += charWidth * 2;

    // Value with fixed width
    char paddedValue[32];
    snprintf(paddedValue, sizeof(paddedValue), "%-*s", MAX_VALUE_WIDTH, displayModeText);
    addString(paddedValue, controlX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getPrimary(), dims.fontSize);
    controlX += PluginUtils::calculateMonospaceTextWidth(MAX_VALUE_WIDTH, dims.fontSize);

    // Right arrow " >"
    addString(" >", controlX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
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

    // Estimate height - much smaller now (tabs + one HUD's controls)
    int estimatedRows = 20;  // Enough for largest HUD (Standings)
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
    addString("HUD SETTINGS", titleX, currentY, Justify::CENTER,
        Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);

    // [X] close button in upper right corner
    float closeButtonTopX = startX + panelWidth - dim.paddingH - PluginUtils::calculateMonospaceTextWidth(3, dim.fontSize);
    addString("[x]", closeButtonTopX, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
    ClickRegion closeRegion;
    closeRegion.x = closeButtonTopX;
    closeRegion.y = currentY;
    closeRegion.width = PluginUtils::calculateMonospaceTextWidth(3, dim.fontSize);
    closeRegion.height = dim.lineHeightNormal;
    closeRegion.type = ClickRegion::CLOSE_BUTTON;
    closeRegion.targetPointer = std::monostate{};
    closeRegion.flagBit = 0;
    closeRegion.isRequired = false;
    closeRegion.targetHud = nullptr;
    closeRegion.tabIndex = 0;
    m_clickRegions.push_back(closeRegion);

    currentY += dim.lineHeightLarge + tabSpacing;

    // Vertical tab bar on left side
    float tabStartX = contentStartX;
    float tabStartY = currentY;
    float tabWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_TAB_WIDTH, dim.fontSize);

    for (int i = 0; i < TAB_COUNT; i++) {
        bool isActive = (i == m_activeTab);
        int tabFont = isActive ? Fonts::ROBOTO_MONO_BOLD : Fonts::ROBOTO_MONO;

        // Check if the HUD for this tab is enabled (visible)
        bool isHudEnabled = false;
        switch (i) {
            case TAB_GENERAL:      isHudEnabled = true; break;  // General tab is always "enabled"
            case TAB_STANDINGS:    isHudEnabled = m_standings && m_standings->isVisible(); break;
            case TAB_MAP:          isHudEnabled = m_mapHud && m_mapHud->isVisible(); break;
            case TAB_PITBOARD:     isHudEnabled = m_pitboard && m_pitboard->isVisible(); break;
            case TAB_LAP_LOG:      isHudEnabled = m_lapLog && m_lapLog->isVisible(); break;
            case TAB_SESSION_BEST: isHudEnabled = m_sessionBest && m_sessionBest->isVisible(); break;
            case TAB_TELEMETRY:    isHudEnabled = m_telemetry && m_telemetry->isVisible(); break;
            case TAB_INPUT:        isHudEnabled = m_input && m_input->isVisible(); break;
            case TAB_PERFORMANCE:  isHudEnabled = m_performance && m_performance->isVisible(); break;
            case TAB_RECORDS:      isHudEnabled = m_records && m_records->isVisible(); break;
            case TAB_WIDGETS:      isHudEnabled = false; break;  // Widgets tab has no single HUD
            case TAB_RADAR:        isHudEnabled = m_radarHud && m_radarHud->isVisible(); break;
        }

        // Color: green if enabled, white if active but disabled, gray if inactive and disabled
        unsigned long tabColor = isHudEnabled ? ColorConfig::getInstance().getPositive() :
                                 isActive ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();

        char tabLabel[20];
        snprintf(tabLabel, sizeof(tabLabel), isActive ? "[%s]" : " %s ",
                 i == TAB_GENERAL ? "General" :
                 i == TAB_STANDINGS ? "Standings" :
                 i == TAB_MAP ? "Map" :
                 i == TAB_LAP_LOG ? "Lap Log" :
                 i == TAB_SESSION_BEST ? "Session Best" :
                 i == TAB_TELEMETRY ? "Telemetry" :
                 i == TAB_INPUT ? "Input" :
                 i == TAB_PERFORMANCE ? "Performance" :
                 i == TAB_PITBOARD ? "Pitboard" :
                 i == TAB_RECORDS ? "Records" :
                 i == TAB_WIDGETS ? "Widgets" :
                 "Radar");

        addString(tabLabel, tabStartX, tabStartY, Justify::LEFT, tabFont, tabColor, dim.fontSize);

        // Add click region for tab
        ClickRegion tabRegion;
        tabRegion.x = tabStartX;
        tabRegion.y = tabStartY;
        tabRegion.width = tabWidth;
        tabRegion.height = dim.lineHeightNormal;
        tabRegion.type = ClickRegion::TAB;
        tabRegion.targetPointer = std::monostate{};
        tabRegion.flagBit = 0;
        tabRegion.isRequired = false;
        tabRegion.targetHud = nullptr;
        tabRegion.tabIndex = i;
        m_clickRegions.push_back(tabRegion);

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
    auto addCycleControl = [&](float baseX, float y, const char* value, int maxValueWidth,
                               ClickRegion::Type downType, ClickRegion::Type upType, BaseHud* targetHud,
                               bool enabled = true) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long arrowColor = enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted();
        unsigned long valueColor = enabled ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();

        // Left arrow "<"
        addString("<", currentX, y, Justify::LEFT, Fonts::ROBOTO_MONO, arrowColor, dim.fontSize);
        if (enabled) {
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                downType, targetHud, 0, false, 0
            ));
        }
        currentX += charWidth * 2;  // "< "

        // Value with fixed width (left-aligned, padded)
        char paddedValue[32];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", maxValueWidth, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::ROBOTO_MONO, valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(maxValueWidth, dim.fontSize);

        // Right arrow " >"
        addString(" >", currentX, y, Justify::LEFT, Fonts::ROBOTO_MONO, arrowColor, dim.fontSize);
        if (enabled) {
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                upType, targetHud, 0, false, 0
            ));
        }
    };

    // Helper lambda to add toggle control with < On/Off > pattern - for boolean settings
    // Both arrows trigger the same toggle action. If enabled is false, muted colors are used.
    auto addToggleControl = [&](float baseX, float y, bool isOn,
                                ClickRegion::Type toggleType, BaseHud* targetHud,
                                uint32_t* bitfield = nullptr, uint32_t flag = 0,
                                bool enabled = true) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long arrowColor = enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted();
        unsigned long valueColor = enabled ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();
        const char* value = isOn ? "On" : "Off";
        constexpr int VALUE_WIDTH = 3;  // "Off" is longest

        // Left arrow "<"
        addString("<", currentX, y, Justify::LEFT, Fonts::ROBOTO_MONO, arrowColor, dim.fontSize);
        if (enabled) {
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
        currentX += charWidth * 2;  // "< "

        // Value with fixed width
        char paddedValue[8];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", VALUE_WIDTH, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::ROBOTO_MONO, valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, dim.fontSize);

        // Right arrow " >"
        addString(" >", currentX, y, Justify::LEFT, Fonts::ROBOTO_MONO, arrowColor, dim.fontSize);
        if (enabled) {
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
        float toggleX = controlX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);  // Align all toggles (longest: "BG Texture")

        // Visibility toggle
        bool isVisible = hud->isVisible();
        addString("Visible", controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
        addToggleControl(toggleX, currentY, isVisible, ClickRegion::HUD_TOGGLE, hud);
        currentY += dim.lineHeightNormal;

        // Title toggle (can be disabled/grayed out)
        bool showTitle = enableTitle ? hud->getShowTitle() : false;
        addString("Title", controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, enableTitle ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        addToggleControl(toggleX, currentY, showTitle, ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, enableTitle);
        currentY += dim.lineHeightNormal;

        // Background texture toggle
        bool showBgTexture = hud->getShowBackgroundTexture();
        addString("BG Texture", controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
        addToggleControl(toggleX, currentY, showBgTexture, ClickRegion::BACKGROUND_TEXTURE_TOGGLE, hud);
        currentY += dim.lineHeightNormal;

        // Background opacity controls
        addString("Opacity", controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char opacityValue[16];
        int opacityPercent = static_cast<int>(hud->getBackgroundOpacity() * 100.0f);
        snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
        addCycleControl(toggleX, currentY, opacityValue, 4,
            ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud);
        currentY += dim.lineHeightNormal;

        // Scale controls
        addString("Scale", controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char scaleValue[16];
        snprintf(scaleValue, sizeof(scaleValue), "%.2f", hud->getScale());
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
        float nameX = contentAreaStartX;
        float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);   // After name
        float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize);   // After Vis toggle (< On >)
        float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // After Title toggle
        float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // After BG Tex toggle
        float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize); // After Opacity cycle

        // Widget name
        addString(name, nameX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getPrimary(), dim.fontSize);

        // Visibility toggle
        bool isVisible = enableVisibility ? hud->isVisible() : false;
        addToggleControl(visX, currentY, isVisible, ClickRegion::HUD_TOGGLE, hud, nullptr, 0, enableVisibility);

        // Title toggle
        bool showTitle = enableTitle ? hud->getShowTitle() : false;
        addToggleControl(titleX, currentY, showTitle, ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, enableTitle);

        // BG Texture toggle
        bool showBgTexture = enableBgTexture ? hud->getShowBackgroundTexture() : false;
        addToggleControl(bgTexX, currentY, showBgTexture, ClickRegion::BACKGROUND_TEXTURE_TOGGLE, hud, nullptr, 0, enableBgTexture);

        // BG Opacity
        char opacityValue[16];
        int opacityPercent = static_cast<int>(hud->getBackgroundOpacity() * 100.0f);
        snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
        addCycleControl(opacityX, currentY, opacityValue, 4,
            ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud, enableOpacity);

        // Scale
        char scaleValue[16];
        snprintf(scaleValue, sizeof(scaleValue), "%.2f", hud->getScale());
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
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::ROBOTO_MONO,
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
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::ROBOTO_MONO,
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
            // General settings tab - color configuration
            ColorConfig& colorConfig = ColorConfig::getInstance();

            // Section header
            addString("Colors", leftColumnX, currentY, Justify::LEFT,
                Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal * 1.5f;

            // Helper lambda to add a color row with preview and cycle buttons
            auto addColorRow = [&](ColorSlot slot) {
                const char* slotName = ColorConfig::getSlotName(slot);
                unsigned long color = colorConfig.getColor(slot);
                const char* colorName = ColorPalette::getColorName(color);

                // Slot name label
                addString(slotName, leftColumnX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

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

                // Cycle buttons and color name
                float buttonX = previewX + previewSize + PluginUtils::calculateMonospaceTextWidth(2, dim.fontSize);

                // [-] button
                addString("<", buttonX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                float buttonWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    buttonX, currentY, buttonWidth, dim.lineHeightNormal,
                    ClickRegion::COLOR_CYCLE_PREV, slot
                ));

                // Color name (centered between buttons)
                float nameX = buttonX + PluginUtils::calculateMonospaceTextWidth(2, dim.fontSize);
                char colorLabel[20];
                snprintf(colorLabel, sizeof(colorLabel), "%-11s", colorName);
                addString(colorLabel, nameX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, color, dim.fontSize);

                // [+] button
                float plusX = nameX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);
                addString(">", plusX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    plusX, currentY, buttonWidth, dim.lineHeightNormal,
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
            addColorRow(ColorSlot::POSITIVE);
            addColorRow(ColorSlot::WARNING);
            addColorRow(ColorSlot::NEUTRAL);
            addColorRow(ColorSlot::NEGATIVE);

            // Units section
            currentY += dim.lineHeightNormal;  // Spacing between sections
            addString("Units", leftColumnX, currentY, Justify::LEFT,
                Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal * 1.5f;

            // Speed unit toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                addString("Speed", leftColumnX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current unit with < > cycle pattern
                const char* unitLabel = (m_speed && m_speed->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH)
                    ? "< km/h >" : "< mph  >";
                addString(unitLabel, toggleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Click region for the entire toggle area
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 8, dim.lineHeightNormal,
                    ClickRegion::SPEED_UNIT_TOGGLE, m_speed
                ));

                currentY += dim.lineHeightNormal;
            }

            // Fuel unit toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                addString("Fuel", leftColumnX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current unit with < > cycle pattern
                const char* unitLabel = (m_fuel && m_fuel->getFuelUnit() == FuelWidget::FuelUnit::GALLONS)
                    ? "< gal  >" : "< L    >";
                addString(unitLabel, toggleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Click region for the entire toggle area
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 8, dim.lineHeightNormal,
                    ClickRegion::FUEL_UNIT_TOGGLE, m_fuel
                ));

                currentY += dim.lineHeightNormal;
            }

            // Positioning section
            currentY += dim.lineHeightNormal;  // Spacing between sections
            addString("Positioning", leftColumnX, currentY, Justify::LEFT,
                Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentY += dim.lineHeightNormal * 1.5f;

            // Grid snap toggle
            {
                float toggleX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                addString("Grid Snap", leftColumnX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Display current state with < > cycle pattern
                bool gridSnapEnabled = ColorConfig::getInstance().getGridSnapping();
                const char* snapLabel = gridSnapEnabled ? "< On  >" : "< Off >";
                addString(snapLabel, toggleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Click region for the entire toggle area
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                m_clickRegions.push_back(ClickRegion(
                    toggleX, currentY, charWidth * 7, dim.lineHeightNormal,
                    ClickRegion::GRID_SNAP_TOGGLE, nullptr
                ));

                currentY += dim.lineHeightNormal;
            }
        }
        break;

        case TAB_STANDINGS:
            activeHud = m_standings;
            dataStartY = addHudControls(m_standings);

            // RIGHT COLUMN: HUD-specific controls and column configuration
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Row count control (specific to StandingsHud)
                addString("Rows", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char rowCountValue[8];
                snprintf(rowCountValue, sizeof(rowCountValue), "%d", m_standings->m_displayRowCount);
                addCycleControl(toggleX, rightY, rowCountValue, 2,
                    ClickRegion::ROW_COUNT_DOWN, ClickRegion::ROW_COUNT_UP, m_standings);
                rightY += dim.lineHeightNormal * 2;  // Extra spacing before column table

                // Column configuration table: Column | Non-Race | Race
                float tableY = rightY;
                float columnNameX = rightColumnX;
                float nonRaceX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(14, dim.fontSize);  // After "Official Gap "
                float raceX = nonRaceX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize);  // After "< On  > "

                // Table header
                addString("Column", columnNameX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Non-Race", nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Race", raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                tableY += dim.lineHeightNormal;

                // Table rows - each row shows column name and two state labels
                struct ColumnRow { const char* name; uint32_t flag; bool raceOnly; bool isGapColumn; };
                ColumnRow columns[] = {
                    {"Position",     StandingsHud::COL_POS,         false, false},
                    {"Race #",       StandingsHud::COL_RACENUM,     false, false},
                    {"Name",         StandingsHud::COL_NAME,        false, false},
                    {"Bike",         StandingsHud::COL_BIKE,        false, false},
                    {"Status",       StandingsHud::COL_STATUS,      false, false},
                    {"Penalty",      StandingsHud::COL_PENALTY,     true,  false},  // Race-only
                    {"Best Lap",     StandingsHud::COL_BEST_LAP,    false, false},
                    {"Official Gap", StandingsHud::COL_OFFICIAL_GAP, false, true},  // Multi-state gap column
                    {"Live Gap",     StandingsHud::COL_LIVE_GAP,    true,  true}   // Multi-state gap column (race-only)
                };

                // Helper lambda to get gap mode label with < > brackets
                auto getGapModeLabel = [](StandingsHud::GapMode mode) -> const char* {
                    switch (mode) {
                        case StandingsHud::GapMode::OFF: return "< Off >";
                        case StandingsHud::GapMode::ME:  return "< Me  >";
                        case StandingsHud::GapMode::ALL: return "< All >";
                        default: return "< Off >";
                    }
                };

                // Helper lambda to add gap mode click region
                auto addGapModeRegion = [&](float x, float y, StandingsHud::GapMode* modePtr) {
                    ClickRegion region;
                    region.x = x;
                    region.y = y;
                    region.width = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);  // "< Off >"
                    region.height = dim.lineHeightNormal;
                    region.type = ClickRegion::GAP_MODE_CYCLE;
                    region.targetPointer = modePtr;
                    region.flagBit = 0;
                    region.isRequired = false;
                    region.targetHud = m_standings;
                    region.tabIndex = 0;
                    m_clickRegions.push_back(region);
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
                    addString(col.name, columnNameX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                    // Non-Race column state
                    if (col.raceOnly) {
                        // Race-only column - show N/A
                        addString("  N/A  ", nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
                    } else if (col.isGapColumn) {
                        // Gap column - show < Off/Me/All >
                        StandingsHud::GapMode* gapModePtr = (col.flag == StandingsHud::COL_OFFICIAL_GAP)
                            ? &m_standings->m_officialGapMode_NonRace
                            : &m_standings->m_liveGapMode_NonRace;
                        const char* label = getGapModeLabel(*gapModePtr);
                        unsigned long color = (*gapModePtr == StandingsHud::GapMode::OFF) ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getSecondary();
                        addString(label, nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addGapModeRegion(nonRaceX, tableY, gapModePtr);
                    } else {
                        // Regular column - show < On/Off >
                        bool enabled = (m_standings->m_nonRaceEnabledColumns & col.flag) != 0;
                        const char* label = enabled ? "< On  >" : "< Off >";
                        unsigned long color = enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted();
                        addString(label, nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addColumnToggle(nonRaceX, tableY, &m_standings->m_nonRaceEnabledColumns, col.flag);
                    }

                    // Race column state
                    if (col.isGapColumn) {
                        // Gap column - show < Off/Me/All >
                        StandingsHud::GapMode* gapModePtr = (col.flag == StandingsHud::COL_OFFICIAL_GAP)
                            ? &m_standings->m_officialGapMode_Race
                            : &m_standings->m_liveGapMode_Race;
                        const char* label = getGapModeLabel(*gapModePtr);
                        unsigned long color = (*gapModePtr == StandingsHud::GapMode::OFF) ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getSecondary();
                        addString(label, raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addGapModeRegion(raceX, tableY, gapModePtr);
                    } else {
                        // Regular column - show < On/Off >
                        bool enabled = (m_standings->m_raceEnabledColumns & col.flag) != 0;
                        const char* label = enabled ? "< On  >" : "< Off >";
                        unsigned long color = enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted();
                        addString(label, raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addColumnToggle(raceX, tableY, &m_standings->m_raceEnabledColumns, col.flag);
                    }

                    tableY += dim.lineHeightNormal;
                }

                // Adjacent rider gaps mode (race-only feature)
                tableY += dim.lineHeightNormal * 0.5f;  // Add some spacing
                addString("Adjacent Gaps", columnNameX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

                // Non-Race column - N/A (race-only feature)
                addString("  N/A  ", nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);

                // Get label for current gap indicator mode
                const char* gapRowsLabel;
                switch (m_standings->m_gapIndicatorMode) {
                    case StandingsHud::GapIndicatorMode::OFF:      gapRowsLabel = "< Off      >"; break;
                    case StandingsHud::GapIndicatorMode::OFFICIAL: gapRowsLabel = "< Official >"; break;
                    case StandingsHud::GapIndicatorMode::LIVE:     gapRowsLabel = "< Live     >"; break;
                    case StandingsHud::GapIndicatorMode::BOTH:     gapRowsLabel = "< Both     >"; break;
                    default: gapRowsLabel = "< Off      >"; break;
                }
                unsigned long gapRowsColor = (m_standings->m_gapIndicatorMode == StandingsHud::GapIndicatorMode::OFF) ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getSecondary();
                addString(gapRowsLabel, raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, gapRowsColor, dim.fontSize);

                ClickRegion region;
                region.x = raceX;
                region.y = tableY;
                region.width = PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);  // "< Official >"
                region.height = dim.lineHeightNormal;
                region.type = ClickRegion::GAP_INDICATOR_CYCLE;
                region.targetPointer = &m_standings->m_gapIndicatorMode;
                region.flagBit = 0;
                region.isRequired = false;
                region.targetHud = m_standings;
                region.tabIndex = 0;
                m_clickRegions.push_back(region);
            }
            break;

        case TAB_MAP:
            activeHud = m_mapHud;
            dataStartY = addHudControls(m_mapHud);

            // RIGHT COLUMN: Map-specific controls
            {
                float rightY = dataStartY;
                float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);

                // Rotation toggle
                addString("Rotate", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_mapHud->getRotateToPlayer(),
                    ClickRegion::MAP_ROTATION_TOGGLE, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Outline toggle
                addString("Outline", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_mapHud->getShowOutline(),
                    ClickRegion::MAP_OUTLINE_TOGGLE, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Colorize riders toggle
                addString("Colorize", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_mapHud->getColorizeRiders(),
                    ClickRegion::MAP_COLORIZE_TOGGLE, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Track line width controls
                addString("Width", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char trackWidthValue[16];
                snprintf(trackWidthValue, sizeof(trackWidthValue), "%.0fm", m_mapHud->getTrackLineWidthMeters());
                addCycleControl(toggleX, rightY, trackWidthValue, 4,  // "9.0m"
                    ClickRegion::MAP_TRACK_WIDTH_DOWN, ClickRegion::MAP_TRACK_WIDTH_UP, m_mapHud);
                rightY += dim.lineHeightNormal;

                // Label mode control
                const char* modeStr = "";
                switch (m_mapHud->getLabelMode()) {
                    case MapHud::LabelMode::NONE:     modeStr = "None"; break;
                    case MapHud::LabelMode::POSITION: modeStr = "Position"; break;
                    case MapHud::LabelMode::RACE_NUM: modeStr = "Race Num"; break;
                    case MapHud::LabelMode::BOTH:     modeStr = "Both"; break;
                    default:
                        DEBUG_WARN_F("Unknown LabelMode: %d", static_cast<int>(m_mapHud->getLabelMode()));
                        modeStr = "Unknown";
                        break;
                }
                addString("Labels", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, modeStr, 8,  // "Race Num" is longest
                    ClickRegion::MAP_LABEL_MODE_CYCLE, ClickRegion::MAP_LABEL_MODE_CYCLE, m_mapHud);
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
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
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

        case TAB_SESSION_BEST:
            activeHud = m_sessionBest;
            dataStartY = addHudControls(m_sessionBest);
            // Right column data toggles (2 items)
            addGroupToggle("Sectors", &m_sessionBest->m_enabledRows,
                SessionBestHud::ROW_S1 | SessionBestHud::ROW_S2 | SessionBestHud::ROW_S3,
                false, m_sessionBest, dataStartY);
            addGroupToggle("Laps", &m_sessionBest->m_enabledRows,
                SessionBestHud::ROW_LAST | SessionBestHud::ROW_BEST | SessionBestHud::ROW_IDEAL,
                false, m_sessionBest, dataStartY + dim.lineHeightNormal);
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
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
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
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
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

        case TAB_WIDGETS:
            // Widgets tab - table format with header row
            {
                // Table header - columns must match addWidgetRow positions exactly
                float nameX = contentAreaStartX;
                float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);
                float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize);   // Match addWidgetRow
                float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // Match addWidgetRow
                float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, dim.fontSize); // Match addWidgetRow
                float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize); // Match addWidgetRow

                addString("Widget", nameX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Vis", visX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Title", titleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("BgTex", bgTexX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Opacity", opacityX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                addString("Scale", scaleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
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
                addWidgetRow("Timing", m_timing, false);  // No title for timing widget
                addWidgetRow("Notices", m_notices, false);  // No title for notices widget
                addWidgetRow("Version", m_version, false, false, false, true, false);  // Only visibility toggle enabled
                addWidgetRow("Fuel", m_fuel);  // Title enabled

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
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char rangeValue[16];
                snprintf(rangeValue, sizeof(rangeValue), "%.0fm", m_radarHud->getRadarRange());
                addCycleControl(toggleX, rightY, rangeValue, 4,  // "100m"
                    ClickRegion::RADAR_RANGE_DOWN, ClickRegion::RADAR_RANGE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Alert distance control (when triangles light up)
                addString("Alert", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                char alertValue[16];
                snprintf(alertValue, sizeof(alertValue), "%.0fm", m_radarHud->getAlertDistance());
                addCycleControl(toggleX, rightY, alertValue, 4,  // "100m"
                    ClickRegion::RADAR_ALERT_DISTANCE_DOWN, ClickRegion::RADAR_ALERT_DISTANCE_UP, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Colorize riders toggle
                addString("Colorize", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_radarHud->getColorizeRiders(),
                    ClickRegion::RADAR_COLORIZE_TOGGLE, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Show player arrow toggle
                addString("Player", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_radarHud->getShowPlayerArrow(),
                    ClickRegion::RADAR_PLAYER_ARROW_TOGGLE, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Auto-hide toggle (fade when no riders nearby)
                addString("Auto-hide", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addToggleControl(toggleX, rightY, m_radarHud->getFadeWhenEmpty(),
                    ClickRegion::RADAR_FADE_TOGGLE, m_radarHud);
                rightY += dim.lineHeightNormal;

                // Label mode control
                const char* modeStr = "";
                switch (m_radarHud->getLabelMode()) {
                    case RadarHud::LabelMode::NONE:     modeStr = "None"; break;
                    case RadarHud::LabelMode::POSITION: modeStr = "Position"; break;
                    case RadarHud::LabelMode::RACE_NUM: modeStr = "Race Num"; break;
                    case RadarHud::LabelMode::BOTH:     modeStr = "Both"; break;
                    default:
                        DEBUG_WARN_F("Unknown LabelMode: %d", static_cast<int>(m_radarHud->getLabelMode()));
                        modeStr = "Unknown";
                        break;
                }
                addString("Labels", rightColumnX, rightY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
                addCycleControl(toggleX, rightY, modeStr, 8,  // "Race Num" is longest
                    ClickRegion::RADAR_LABEL_MODE_CYCLE, ClickRegion::RADAR_LABEL_MODE_CYCLE, m_radarHud);
                rightY += dim.lineHeightNormal;
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
    addString("[Close]", closeButtonBottomX, closeButtonBottomY, Justify::CENTER,
        Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
    m_clickRegions.push_back(ClickRegion(
        closeButtonBottomX - PluginUtils::calculateMonospaceTextWidth(3, dim.fontSize),  // Half of "[Close]" width (7 chars)
        closeButtonBottomY,
        PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize),
        dim.lineHeightNormal,
        ClickRegion::CLOSE_BUTTON, nullptr, 0, false, 0
    ));

    // Restore Defaults button - bottom right corner, same row as Close button
    float resetButtonY = closeButtonBottomY;
    float resetButtonX = startX + panelWidth - dim.paddingH - PluginUtils::calculateMonospaceTextWidth(SettingsHud::RESET_BUTTON_WIDTH, dim.fontSize);
    addString("[Restore Defaults]", resetButtonX, resetButtonY, Justify::LEFT,
        Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getSecondary(), dim.fontSize);
    m_clickRegions.push_back(ClickRegion(
        resetButtonX,
        resetButtonY,
        PluginUtils::calculateMonospaceTextWidth(SettingsHud::RESET_BUTTON_WIDTH, dim.fontSize),
        dim.lineHeightNormal,
        ClickRegion::RESET_BUTTON, nullptr, 0, false, 0
    ));
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
                case ClickRegion::GAP_MODE_CYCLE:
                    handleGapModeClick(region);
                    break;
                case ClickRegion::GAP_INDICATOR_CYCLE:
                    handleGapIndicatorClick(region);
                    break;
                case ClickRegion::HUD_TOGGLE:
                    handleHudToggleClick(region);
                    break;
                case ClickRegion::TITLE_TOGGLE:
                    handleTitleToggleClick(region);
                    break;
                case ClickRegion::BACKGROUND_TEXTURE_TOGGLE:
                    handleBackgroundTextureToggleClick(region);
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
                case ClickRegion::MAP_COLORIZE_TOGGLE:
                    handleMapColorizeClick(region);
                    break;
                case ClickRegion::MAP_TRACK_WIDTH_UP:
                    handleMapTrackWidthClick(region, true);
                    break;
                case ClickRegion::MAP_TRACK_WIDTH_DOWN:
                    handleMapTrackWidthClick(region, false);
                    break;
                case ClickRegion::MAP_LABEL_MODE_CYCLE:
                    handleMapLabelModeClick(region);
                    break;
                case ClickRegion::RADAR_RANGE_UP:
                    handleRadarRangeClick(region, true);
                    break;
                case ClickRegion::RADAR_RANGE_DOWN:
                    handleRadarRangeClick(region, false);
                    break;
                case ClickRegion::RADAR_COLORIZE_TOGGLE:
                    handleRadarColorizeClick(region);
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
                case ClickRegion::RADAR_LABEL_MODE_CYCLE:
                    handleRadarLabelModeClick(region);
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
                case ClickRegion::COLOR_CYCLE_NEXT:
                    handleColorCycleClick(region, true);
                    break;
                case ClickRegion::COLOR_CYCLE_PREV:
                    handleColorCycleClick(region, false);
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
                case ClickRegion::RESET_BUTTON:
                    resetToDefaults();
                    DEBUG_INFO("All settings reset to defaults");
                    break;
                case ClickRegion::TAB:
                    handleTabClick(region);
                    return;  // Don't save settings, just UI state change
                case ClickRegion::CLOSE_BUTTON:
                    handleCloseButtonClick();
                    return;  // Don't save settings, just close the menu

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

void SettingsHud::resetToDefaults() {
    // Reset all HUDs to their constructor defaults
    if (m_sessionBest) m_sessionBest->resetToDefaults();
    if (m_lapLog) m_lapLog->resetToDefaults();
    if (m_standings) m_standings->resetToDefaults();
    if (m_performance) m_performance->resetToDefaults();
    if (m_telemetry) m_telemetry->resetToDefaults();
    if (m_input) m_input->resetToDefaults();
    if (m_mapHud) m_mapHud->resetToDefaults();
    if (m_radarHud) m_radarHud->resetToDefaults();
    if (m_pitboard) m_pitboard->resetToDefaults();
    if (m_records) m_records->resetToDefaults();

    // Reset all widgets to their constructor defaults
    if (m_lap) m_lap->resetToDefaults();
    if (m_position) m_position->resetToDefaults();
    if (m_time) m_time->resetToDefaults();
    if (m_session) m_session->resetToDefaults();
    if (m_speed) m_speed->resetToDefaults();
    if (m_speedo) m_speedo->resetToDefaults();
    if (m_tacho) m_tacho->resetToDefaults();
    if (m_timing) m_timing->resetToDefaults();
    if (m_notices) m_notices->resetToDefaults();
    if (m_bars) m_bars->resetToDefaults();
    if (m_version) m_version->resetToDefaults();
    if (m_fuel) m_fuel->resetToDefaults();

    // Reset settings button (managed by HudManager)
    HudManager::getInstance().getSettingsButtonWidget().resetToDefaults();

    // Reset color configuration
    ColorConfig::getInstance().resetToDefaults();

    // Update settings display
    rebuildRenderData();

    // Save settings after reset
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

void SettingsHud::handleGapModeClick(const ClickRegion& region) {
    auto* gapMode = std::get_if<StandingsHud::GapMode*>(&region.targetPointer);
    if (!gapMode || !*gapMode || !region.targetHud) return;

    StandingsHud::GapMode oldMode = **gapMode;
    switch (**gapMode) {
        case StandingsHud::GapMode::OFF:
            **gapMode = StandingsHud::GapMode::ME;
            break;
        case StandingsHud::GapMode::ME:
            **gapMode = StandingsHud::GapMode::ALL;
            break;
        case StandingsHud::GapMode::ALL:
            **gapMode = StandingsHud::GapMode::OFF;
            break;
        default:
            DEBUG_WARN_F("Invalid GapMode: %d, resetting to OFF", static_cast<int>(**gapMode));
            **gapMode = StandingsHud::GapMode::OFF;
            break;
    }
    region.targetHud->setDataDirty();
    rebuildRenderData();
    DEBUG_INFO_F("Gap mode cycled: %d -> %d", static_cast<int>(oldMode), static_cast<int>(**gapMode));
}

void SettingsHud::handleGapIndicatorClick(const ClickRegion& region) {
    auto* gapIndicatorMode = std::get_if<StandingsHud::GapIndicatorMode*>(&region.targetPointer);
    if (!gapIndicatorMode || !*gapIndicatorMode || !region.targetHud) return;

    StandingsHud::GapIndicatorMode oldMode = **gapIndicatorMode;
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
            DEBUG_WARN_F("Invalid GapIndicatorMode: %d, resetting to OFF", static_cast<int>(**gapIndicatorMode));
            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
            break;
    }
    region.targetHud->setDataDirty();
    rebuildRenderData();
    DEBUG_INFO_F("Gap indicator mode cycled: %d -> %d", static_cast<int>(oldMode), static_cast<int>(**gapIndicatorMode));
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

void SettingsHud::handleBackgroundTextureToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;

    region.targetHud->setShowBackgroundTexture(!region.targetHud->getShowBackgroundTexture());
    rebuildRenderData();
    DEBUG_INFO_F("HUD background texture toggled: %s", region.targetHud->getShowBackgroundTexture() ? "enabled" : "disabled");
}

void SettingsHud::handleOpacityClick(const ClickRegion& region, bool increase) {
    if (!region.targetHud) return;

    float newOpacity = region.targetHud->getBackgroundOpacity() + (increase ? 0.10f : -0.10f);
    if (newOpacity > 1.0f) newOpacity = 1.0f;
    if (newOpacity < 0.0f) newOpacity = 0.0f;
    region.targetHud->setBackgroundOpacity(newOpacity);
    rebuildRenderData();
    DEBUG_INFO_F("HUD background opacity %s to %d%%",
        increase ? "increased" : "decreased", static_cast<int>(newOpacity * 100.0f));
}

void SettingsHud::handleScaleClick(const ClickRegion& region, bool increase) {
    if (!region.targetHud) return;

    float newScale = region.targetHud->getScale() + (increase ? 0.1f : -0.1f);
    if (newScale > 3.0f) newScale = 3.0f;
    if (newScale < 0.1f) newScale = 0.1f;
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

void SettingsHud::handleMapColorizeClick(const ClickRegion& region) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        bool newColorize = !mapHud->getColorizeRiders();
        mapHud->setColorizeRiders(newColorize);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud colorize riders %s", newColorize ? "enabled" : "disabled");
    }
}

void SettingsHud::handleMapTrackWidthClick(const ClickRegion& region, bool increase) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        float newWidth = mapHud->getTrackLineWidthMeters() + (increase ? 1.0f : -1.0f);
        mapHud->setTrackLineWidthMeters(newWidth);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud track line width %s to %.0fm", increase ? "increased" : "decreased", newWidth);
    }
}

void SettingsHud::handleMapLabelModeClick(const ClickRegion& region) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);
    if (mapHud) {
        MapHud::LabelMode currentMode = mapHud->getLabelMode();
        MapHud::LabelMode newMode;

        // Cycle: NONE -> POSITION -> RACE_NUM -> BOTH -> NONE
        switch (currentMode) {
            case MapHud::LabelMode::NONE:     newMode = MapHud::LabelMode::POSITION; break;
            case MapHud::LabelMode::POSITION: newMode = MapHud::LabelMode::RACE_NUM; break;
            case MapHud::LabelMode::RACE_NUM: newMode = MapHud::LabelMode::BOTH; break;
            case MapHud::LabelMode::BOTH:     newMode = MapHud::LabelMode::NONE; break;
            default:                          newMode = MapHud::LabelMode::POSITION; break;
        }

        mapHud->setLabelMode(newMode);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud label mode changed to %d", static_cast<int>(newMode));
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

void SettingsHud::handleRadarColorizeClick(const ClickRegion& region) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        bool newColorize = !radarHud->getColorizeRiders();
        radarHud->setColorizeRiders(newColorize);
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud colorize riders %s", newColorize ? "enabled" : "disabled");
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

void SettingsHud::handleRadarLabelModeClick(const ClickRegion& region) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    if (radarHud) {
        RadarHud::LabelMode currentMode = radarHud->getLabelMode();
        RadarHud::LabelMode newMode;

        // Cycle: NONE -> POSITION -> RACE_NUM -> BOTH -> NONE
        switch (currentMode) {
            case RadarHud::LabelMode::NONE:     newMode = RadarHud::LabelMode::POSITION; break;
            case RadarHud::LabelMode::POSITION: newMode = RadarHud::LabelMode::RACE_NUM; break;
            case RadarHud::LabelMode::RACE_NUM: newMode = RadarHud::LabelMode::BOTH; break;
            case RadarHud::LabelMode::BOTH:     newMode = RadarHud::LabelMode::NONE; break;
            default:                            newMode = RadarHud::LabelMode::POSITION; break;
        }

        radarHud->setLabelMode(newMode);
        rebuildRenderData();
        DEBUG_INFO_F("RadarHud label mode changed to %d", static_cast<int>(newMode));
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
    if (m_standings) m_standings->setDataDirty();
    if (m_mapHud) m_mapHud->setDataDirty();
    if (m_radarHud) m_radarHud->setDataDirty();
    if (m_lapLog) m_lapLog->setDataDirty();
    if (m_sessionBest) m_sessionBest->setDataDirty();
    if (m_telemetry) m_telemetry->setDataDirty();
    if (m_input) m_input->setDataDirty();
    if (m_performance) m_performance->setDataDirty();
    if (m_pitboard) m_pitboard->setDataDirty();
    if (m_records) m_records->setDataDirty();
    if (m_time) m_time->setDataDirty();
    if (m_position) m_position->setDataDirty();
    if (m_lap) m_lap->setDataDirty();
    if (m_session) m_session->setDataDirty();
    if (m_speed) m_speed->setDataDirty();
    if (m_speedo) m_speedo->setDataDirty();
    if (m_tacho) m_tacho->setDataDirty();
    if (m_timing) m_timing->setDataDirty();
    if (m_bars) m_bars->setDataDirty();
    if (m_notices) m_notices->setDataDirty();
    if (m_version) m_version->setDataDirty();
    if (m_fuel) m_fuel->setDataDirty();

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
