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
                         TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionWidget* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, SpeedoWidget* speedo, TachoWidget* tacho, TimingWidget* timing, BarsWidget* bars, VersionWidget* version, NoticesWidget* notices, PitboardHud* pitboard, FuelWidget* fuel)
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
      m_fuel(fuel),
      m_bVisible(false),
      m_cachedWindowWidth(0),
      m_cachedWindowHeight(0),
      m_activeTab(TAB_STANDINGS)
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
    // Renders "Display: [Mode]" with [-] and [+] cycle buttons
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

    // Render label with current mode
    char displayModeLabel[32];
    snprintf(displayModeLabel, sizeof(displayModeLabel), "Display: %s", displayModeText);
    addString(displayModeLabel, x, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);

    // Calculate button positions
    float displayModeButtonX = x + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dims.fontSize);
    float minusX = displayModeButtonX;
    float plusX = displayModeButtonX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_BUTTON_GAP, dims.fontSize);
    float buttonWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dims.fontSize);

    // Render [-] button
    addString("[-]", minusX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
    addClickRegion(ClickRegion::DISPLAY_MODE_DOWN, minusX, currentY, buttonWidth, dims.lineHeightNormal,
                   targetHud, nullptr, displayMode, 0, false, 0);

    // Render [+] button
    addString("[+]", plusX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
    addClickRegion(ClickRegion::DISPLAY_MODE_UP, plusX, currentY, buttonWidth, dims.lineHeightNormal,
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
        Fonts::ENTER_SANSMAN, TextColors::PRIMARY, dim.fontSizeLarge);

    // [X] close button in upper right corner
    float closeButtonTopX = startX + panelWidth - dim.paddingH - PluginUtils::calculateMonospaceTextWidth(3, dim.fontSize);
    addString("[x]", closeButtonTopX, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
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
            case TAB_STANDINGS:    isHudEnabled = m_standings && m_standings->isVisible(); break;
            case TAB_MAP:          isHudEnabled = m_mapHud && m_mapHud->isVisible(); break;
            case TAB_PITBOARD:     isHudEnabled = m_pitboard && m_pitboard->isVisible(); break;
            case TAB_LAP_LOG:      isHudEnabled = m_lapLog && m_lapLog->isVisible(); break;
            case TAB_SESSION_BEST: isHudEnabled = m_sessionBest && m_sessionBest->isVisible(); break;
            case TAB_TELEMETRY:    isHudEnabled = m_telemetry && m_telemetry->isVisible(); break;
            case TAB_INPUT:        isHudEnabled = m_input && m_input->isVisible(); break;
            case TAB_PERFORMANCE:  isHudEnabled = m_performance && m_performance->isVisible(); break;
            case TAB_WIDGETS:      isHudEnabled = false; break;  // Widgets tab has no single HUD
            case TAB_RADAR:        isHudEnabled = m_radarHud && m_radarHud->isVisible(); break;
        }

        // Color: green if enabled, white if active but disabled, gray if inactive and disabled
        unsigned long tabColor = isHudEnabled ? SemanticColors::POSITIVE :
                                 isActive ? TextColors::PRIMARY : TextColors::MUTED;

        char tabLabel[20];
        snprintf(tabLabel, sizeof(tabLabel), isActive ? "[%s]" : " %s ",
                 i == TAB_STANDINGS ? "Standings" :
                 i == TAB_MAP ? "Map" :
                 i == TAB_LAP_LOG ? "Lap Log" :
                 i == TAB_SESSION_BEST ? "Session Best" :
                 i == TAB_TELEMETRY ? "Telemetry" :
                 i == TAB_INPUT ? "Input" :
                 i == TAB_PERFORMANCE ? "Performance" :
                 i == TAB_PITBOARD ? "Pitboard" :
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

    // Helper lambda to add control buttons (minus/plus pair) - shared across all controls
    auto addControlButtons = [&](float baseX, float y, ClickRegion::Type downType, ClickRegion::Type upType, BaseHud* targetHud) {
        float minusX = baseX;
        float plusX = baseX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_BUTTON_GAP, dim.fontSize);

        addString("[-]", minusX, y, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
        m_clickRegions.push_back(ClickRegion(
            minusX, y,
            PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize),
            dim.lineHeightNormal,
            downType, targetHud, 0, false, 0
        ));

        addString("[+]", plusX, y, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
        m_clickRegions.push_back(ClickRegion(
            plusX, y,
            PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize),
            dim.lineHeightNormal,
            upType, targetHud, 0, false, 0
        ));
    };

    auto addHudControls = [&](BaseHud* hud, bool enableTitle = true) -> float {
        // Save starting Y for right column (data toggles)
        float sectionStartY = currentY;

        // LEFT COLUMN: Basic controls
        float controlX = leftColumnX;

        // Visibility checkbox
        bool isVisible = hud->isVisible();
        const char* visCheckbox = isVisible ? "[X]" : "[ ]";
        addString(visCheckbox, controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
        float visLabelX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
        addString("Visible", visLabelX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

        m_clickRegions.push_back(ClickRegion(
            controlX, currentY,
            PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_SMALL, dim.fontSize),
            dim.lineHeightNormal,
            ClickRegion::HUD_TOGGLE, hud, 0, false, 0
        ));
        currentY += dim.lineHeightNormal;

        // Title checkbox (can be disabled/grayed out)
        bool showTitle = enableTitle ? hud->getShowTitle() : false;
        const char* titleCheckbox = showTitle ? "[X]" : "[ ]";
        unsigned long titleColor = enableTitle ? TextColors::SECONDARY : TextColors::MUTED;
        addString(titleCheckbox, controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, titleColor, dim.fontSize);
        addString("Show Title", visLabelX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, titleColor, dim.fontSize);

        if (enableTitle) {
            m_clickRegions.push_back(ClickRegion(
                controlX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_MEDIUM, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::TITLE_TOGGLE, hud, 0, false, 0
            ));
        }
        currentY += dim.lineHeightNormal;

        // Background texture checkbox
        bool showBgTexture = hud->getShowBackgroundTexture();
        const char* bgTexCheckbox = showBgTexture ? "[X]" : "[ ]";
        addString(bgTexCheckbox, controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
        addString("BG Texture", visLabelX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

        m_clickRegions.push_back(ClickRegion(
            controlX, currentY,
            PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_MEDIUM, dim.fontSize),
            dim.lineHeightNormal,
            ClickRegion::BACKGROUND_TEXTURE_TOGGLE, hud, 0, false, 0
        ));
        currentY += dim.lineHeightNormal;

        // Background opacity controls
        char opacityText[32];
        int opacityPercent = static_cast<int>(hud->getBackgroundOpacity() * 100.0f);
        snprintf(opacityText, sizeof(opacityText), "Opacity: %d%%", opacityPercent);
        addString(opacityText, controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

        float opacityButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
        addControlButtons(opacityButtonX, currentY, ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud);

        currentY += dim.lineHeightNormal;

        // Scale controls
        char scaleText[32];
        snprintf(scaleText, sizeof(scaleText), "Scale: %.2f", hud->getScale());
        addString(scaleText, controlX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

        float scaleButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
        addControlButtons(scaleButtonX, currentY, ClickRegion::SCALE_DOWN, ClickRegion::SCALE_UP, hud);

        currentY += dim.lineHeightNormal;

        // Return the starting Y for right column (data toggles)
        return sectionStartY;
    };

    // Widget table row - displays widget settings in columnar format
    auto addWidgetRow = [&](const char* name, BaseHud* hud, bool enableTitle = true, bool enableOpacity = true, bool enableScale = true, bool enableVisibility = true, bool enableBgTexture = true) {
        // Column positions (spacing for table layout)
        float nameX = contentAreaStartX;
        float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);  // After name
        float titleX = visX + PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize);  // After visibility checkbox
        float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);  // After title checkbox
        float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(6, dim.fontSize);  // After BG Texture checkbox
        float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(14, dim.fontSize);  // After opacity controls

        // Widget name
        addString(name, nameX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, TextColors::PRIMARY, dim.fontSize);

        // Visibility checkbox
        bool isVisible = enableVisibility ? hud->isVisible() : false;  // Force unchecked when disabled
        const char* visCheckbox = isVisible ? "[X]" : "[ ]";
        unsigned long visColor = enableVisibility ? TextColors::SECONDARY : TextColors::MUTED;
        addString(visCheckbox, visX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, visColor, dim.fontSize);

        // Only add click region if visibility is enabled for this widget
        if (enableVisibility) {
            m_clickRegions.push_back(ClickRegion(
                visX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::HUD_TOGGLE, hud, 0, false, 0
            ));
        }

        // Title checkbox
        bool showTitle = enableTitle ? hud->getShowTitle() : false;  // Force unchecked when disabled
        const char* titleCheckbox = showTitle ? "[X]" : "[ ]";
        unsigned long titleColor = enableTitle ? TextColors::SECONDARY : TextColors::MUTED;
        addString(titleCheckbox, titleX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, titleColor, dim.fontSize);

        // Only add click region if title is enabled for this widget
        if (enableTitle) {
            m_clickRegions.push_back(ClickRegion(
                titleX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::TITLE_TOGGLE, hud, 0, false, 0
            ));
        }

        // BG Texture checkbox
        bool showBgTexture = enableBgTexture ? hud->getShowBackgroundTexture() : false;
        const char* bgTexCheckbox = showBgTexture ? "[X]" : "[ ]";
        unsigned long bgTexColor = enableBgTexture ? TextColors::SECONDARY : TextColors::MUTED;
        addString(bgTexCheckbox, bgTexX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, bgTexColor, dim.fontSize);

        if (enableBgTexture) {
            m_clickRegions.push_back(ClickRegion(
                bgTexX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::BACKGROUND_TEXTURE_TOGGLE, hud, 0, false, 0
            ));
        }

        // BG Opacity
        char opacityText[16];
        int opacityPercent = static_cast<int>(hud->getBackgroundOpacity() * 100.0f);
        snprintf(opacityText, sizeof(opacityText), "%3d%%", opacityPercent);
        unsigned long opacityColor = enableOpacity ? TextColors::SECONDARY : TextColors::MUTED;
        addString(opacityText, opacityX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, opacityColor, dim.fontSize);

        float opacityButtonX = opacityX + PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize);
        addString("[-]", opacityButtonX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, opacityColor, dim.fontSize);
        if (enableOpacity) {
            m_clickRegions.push_back(ClickRegion(
                opacityButtonX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::BACKGROUND_OPACITY_DOWN, hud, 0, false, 0
            ));
        }

        float opacityPlusX = opacityButtonX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_BUTTON_GAP, dim.fontSize);
        addString("[+]", opacityPlusX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, opacityColor, dim.fontSize);
        if (enableOpacity) {
            m_clickRegions.push_back(ClickRegion(
                opacityPlusX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::BACKGROUND_OPACITY_UP, hud, 0, false, 0
            ));
        }

        // Scale
        char scaleText[16];
        snprintf(scaleText, sizeof(scaleText), "%.2f", hud->getScale());
        unsigned long scaleColor = enableScale ? TextColors::SECONDARY : TextColors::MUTED;
        addString(scaleText, scaleX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, scaleColor, dim.fontSize);

        float scaleButtonX = scaleX + PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize);
        addString("[-]", scaleButtonX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, scaleColor, dim.fontSize);
        if (enableScale) {
            m_clickRegions.push_back(ClickRegion(
                scaleButtonX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::SCALE_DOWN, hud, 0, false, 0
            ));
        }

        float scalePlusX = scaleButtonX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_BUTTON_GAP, dim.fontSize);
        addString("[+]", scalePlusX, currentY, Justify::LEFT,
            Fonts::ROBOTO_MONO, scaleColor, dim.fontSize);
        if (enableScale) {
            m_clickRegions.push_back(ClickRegion(
                scalePlusX, currentY,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::SCALE_UP, hud, 0, false, 0
            ));
        }

        currentY += dim.lineHeightNormal;
    };

    auto addDataCheckbox = [&](const char* label, uint32_t* bitfield, uint32_t flag, bool isRequired, BaseHud* hud, float yPos) {
        float dataX = rightColumnX;
        bool isChecked = (*bitfield & flag) != 0;
        const char* checkbox = isRequired ? "[*]" : (isChecked ? "[X]" : "[ ]");
        unsigned long color = isRequired ? TextColors::MUTED : TextColors::SECONDARY;

        addString(checkbox, dataX, yPos, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
        float labelX = dataX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
        addString(label, labelX, yPos, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);

        if (!isRequired) {
            m_clickRegions.push_back(ClickRegion(
                dataX, yPos,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_CLICKABLE, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::CHECKBOX, bitfield, flag, false, hud
            ));
        }
    };

    // Helper for grouped checkboxes that toggle multiple bits
    auto addGroupCheckbox = [&](const char* label, uint32_t* bitfield, uint32_t groupFlags, bool isRequired, BaseHud* hud, float yPos) {
        float dataX = rightColumnX;
        // Group is checked if all bits in group are set
        bool isChecked = (*bitfield & groupFlags) == groupFlags;
        const char* checkbox = isRequired ? "[*]" : (isChecked ? "[X]" : "[ ]");
        unsigned long color = isRequired ? TextColors::MUTED : TextColors::SECONDARY;

        addString(checkbox, dataX, yPos, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
        float labelX = dataX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
        addString(label, labelX, yPos, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);

        if (!isRequired) {
            m_clickRegions.push_back(ClickRegion(
                dataX, yPos,
                PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_CLICKABLE, dim.fontSize),
                dim.lineHeightNormal,
                ClickRegion::CHECKBOX, bitfield, groupFlags, false, hud
            ));
        }
    };

    // Render controls for active tab only
    BaseHud* activeHud = nullptr;
    float dataStartY = 0.0f;

    switch (m_activeTab) {
        case TAB_STANDINGS:
            activeHud = m_standings;
            dataStartY = addHudControls(m_standings);

            // Row count control (specific to StandingsHud)
            {
                float controlX = leftColumnX;
                char rowCountText[32];
                snprintf(rowCountText, sizeof(rowCountText), "Rows: %d", m_standings->m_displayRowCount);
                addString(rowCountText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                float rowCountButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
                addControlButtons(rowCountButtonX, currentY, ClickRegion::ROW_COUNT_DOWN, ClickRegion::ROW_COUNT_UP, m_standings);

                currentY += dim.lineHeightNormal;
            }

            // Column configuration table: Column | Non-Race | Race
            {
                float tableY = dataStartY;
                float columnNameX = rightColumnX;
                float nonRaceX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(14, dim.fontSize);  // After "Official Gap "
                float raceX = nonRaceX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);  // After "Non-Race  "

                // Table header
                addString("Column", columnNameX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("Non-Race", nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("Race", raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
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

                // Helper lambda to get gap mode label
                auto getGapModeLabel = [](StandingsHud::GapMode mode) -> const char* {
                    switch (mode) {
                        case StandingsHud::GapMode::OFF: return "Off";
                        case StandingsHud::GapMode::ME: return "Me";
                        case StandingsHud::GapMode::ALL: return "All";
                        default: return "Off";
                    }
                };

                // Helper lambda to add gap mode click region
                auto addGapModeRegion = [&](float x, float y, StandingsHud::GapMode* modePtr) {
                    ClickRegion region;
                    region.x = x;
                    region.y = y;
                    region.width = PluginUtils::calculateMonospaceTextWidth(4, dim.fontSize);
                    region.height = dim.lineHeightNormal;
                    region.type = ClickRegion::GAP_MODE_CYCLE;
                    region.targetPointer = modePtr;
                    region.flagBit = 0;
                    region.isRequired = false;
                    region.targetHud = m_standings;
                    region.tabIndex = 0;
                    m_clickRegions.push_back(region);
                };

                // Helper lambda to add column checkbox region
                auto addColumnCheckbox = [&](float x, float y, uint32_t* bitfield, uint32_t flag) {
                    ClickRegion region;
                    region.x = x;
                    region.y = y;
                    region.width = PluginUtils::calculateMonospaceTextWidth(3, dim.fontSize);
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
                    addString(col.name, columnNameX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                    // Non-Race column state
                    if (col.raceOnly) {
                        // Race-only column - show N/A
                        addString("N/A", nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::MUTED, dim.fontSize);
                    } else if (col.isGapColumn) {
                        // Gap column - show Off/Me/All
                        StandingsHud::GapMode* gapModePtr = (col.flag == StandingsHud::COL_OFFICIAL_GAP)
                            ? &m_standings->m_officialGapMode_NonRace
                            : &m_standings->m_liveGapMode_NonRace;
                        const char* label = getGapModeLabel(*gapModePtr);
                        unsigned long color = (*gapModePtr == StandingsHud::GapMode::OFF) ? TextColors::MUTED : TextColors::SECONDARY;
                        addString(label, nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addGapModeRegion(nonRaceX, tableY, gapModePtr);
                    } else {
                        // Regular column - show On/Off
                        bool enabled = (m_standings->m_nonRaceEnabledColumns & col.flag) != 0;
                        const char* label = enabled ? "On" : "Off";
                        unsigned long color = enabled ? TextColors::SECONDARY : TextColors::MUTED;
                        addString(label, nonRaceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addColumnCheckbox(nonRaceX, tableY, &m_standings->m_nonRaceEnabledColumns, col.flag);
                    }

                    // Race column state
                    if (col.isGapColumn) {
                        // Gap column - show Off/Me/All
                        StandingsHud::GapMode* gapModePtr = (col.flag == StandingsHud::COL_OFFICIAL_GAP)
                            ? &m_standings->m_officialGapMode_Race
                            : &m_standings->m_liveGapMode_Race;
                        const char* label = getGapModeLabel(*gapModePtr);
                        unsigned long color = (*gapModePtr == StandingsHud::GapMode::OFF) ? TextColors::MUTED : TextColors::SECONDARY;
                        addString(label, raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addGapModeRegion(raceX, tableY, gapModePtr);
                    } else {
                        // Regular column - show On/Off
                        bool enabled = (m_standings->m_raceEnabledColumns & col.flag) != 0;
                        const char* label = enabled ? "On" : "Off";
                        unsigned long color = enabled ? TextColors::SECONDARY : TextColors::MUTED;
                        addString(label, raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, color, dim.fontSize);
                        addColumnCheckbox(raceX, tableY, &m_standings->m_raceEnabledColumns, col.flag);
                    }

                    tableY += dim.lineHeightNormal;
                }

                // Adjacent rider gaps mode (race-only feature)
                tableY += dim.lineHeightNormal * 0.5f;  // Add some spacing
                addString("Adjacent Rider Gaps", columnNameX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                // Get label for current gap indicator mode
                const char* gapRowsLabel;
                switch (m_standings->m_gapIndicatorMode) {
                    case StandingsHud::GapIndicatorMode::OFF:      gapRowsLabel = "Off"; break;
                    case StandingsHud::GapIndicatorMode::OFFICIAL: gapRowsLabel = "Official"; break;
                    case StandingsHud::GapIndicatorMode::LIVE:     gapRowsLabel = "Live"; break;
                    case StandingsHud::GapIndicatorMode::BOTH:     gapRowsLabel = "Both"; break;
                    default: gapRowsLabel = "Off"; break;
                }
                unsigned long gapRowsColor = (m_standings->m_gapIndicatorMode == StandingsHud::GapIndicatorMode::OFF) ? TextColors::MUTED : TextColors::SECONDARY;
                addString(gapRowsLabel, raceX, tableY, Justify::LEFT, Fonts::ROBOTO_MONO, gapRowsColor, dim.fontSize);

                ClickRegion region;
                region.x = raceX;
                region.y = tableY;
                region.width = PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize);  // "Official" is longest at 8 chars + padding
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

            // Add rotation toggle checkbox
            {
                float controlX = leftColumnX;
                float rotationY = currentY;

                bool rotateToPlayer = m_mapHud->getRotateToPlayer();
                const char* rotationCheckbox = rotateToPlayer ? "[X]" : "[ ]";
                addString(rotationCheckbox, controlX, rotationY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
                float rotationLabelX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
                addString("Rotate", rotationLabelX, rotationY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                m_clickRegions.push_back(ClickRegion(
                    controlX, rotationY,
                    PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_MEDIUM, dim.fontSize),
                    dim.lineHeightNormal,
                    ClickRegion::MAP_ROTATION_TOGGLE, m_mapHud, 0, false, 0
                ));

                currentY += dim.lineHeightNormal;
            }

            // Add outline toggle checkbox
            {
                float controlX = leftColumnX;
                float outlineY = currentY;

                bool showOutline = m_mapHud->getShowOutline();
                const char* outlineCheckbox = showOutline ? "[X]" : "[ ]";
                addString(outlineCheckbox, controlX, outlineY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
                float outlineLabelX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
                addString("Outline", outlineLabelX, outlineY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                m_clickRegions.push_back(ClickRegion(
                    controlX, outlineY,
                    PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_MEDIUM, dim.fontSize),
                    dim.lineHeightNormal,
                    ClickRegion::MAP_OUTLINE_TOGGLE, m_mapHud, 0, false, 0
                ));

                currentY += dim.lineHeightNormal;
            }

            // Add colorize riders toggle checkbox
            {
                float controlX = leftColumnX;
                float colorizeY = currentY;

                bool colorizeRiders = m_mapHud->getColorizeRiders();
                const char* colorizeCheckbox = colorizeRiders ? "[X]" : "[ ]";
                addString(colorizeCheckbox, controlX, colorizeY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
                float colorizeLabelX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
                addString("Colorize", colorizeLabelX, colorizeY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                m_clickRegions.push_back(ClickRegion(
                    controlX, colorizeY,
                    PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_MEDIUM, dim.fontSize),
                    dim.lineHeightNormal,
                    ClickRegion::MAP_COLORIZE_TOGGLE, m_mapHud, 0, false, 0
                ));

                currentY += dim.lineHeightNormal;
            }

            // Add track line width controls
            {
                float controlX = leftColumnX;
                char trackWidthText[32];
                snprintf(trackWidthText, sizeof(trackWidthText), "Width: %.1fm", m_mapHud->getTrackLineWidthMeters());
                addString(trackWidthText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                float trackWidthButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
                addControlButtons(trackWidthButtonX, currentY, ClickRegion::MAP_TRACK_WIDTH_DOWN, ClickRegion::MAP_TRACK_WIDTH_UP, m_mapHud);

                currentY += dim.lineHeightNormal;
            }

            // Add label mode control
            {
                float controlX = leftColumnX;
                char labelModeText[32];
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
                snprintf(labelModeText, sizeof(labelModeText), "Labels: %s", modeStr);
                addString(labelModeText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                // Click region for cycling through modes
                m_clickRegions.push_back(ClickRegion(
                    controlX, currentY,
                    PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH + 3, dim.fontSize),
                    dim.lineHeightNormal,
                    ClickRegion::MAP_LABEL_MODE_CYCLE, m_mapHud, 0, false, 0
                ));

                currentY += dim.lineHeightNormal;
            }
            break;

        case TAB_LAP_LOG:
            activeHud = m_lapLog;
            dataStartY = addHudControls(m_lapLog);

            // Row count control (specific to LapLogHud)
            {
                float controlX = leftColumnX;
                char rowCountText[32];
                snprintf(rowCountText, sizeof(rowCountText), "Rows: %d", m_lapLog->m_maxDisplayLaps);
                addString(rowCountText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                float rowCountButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
                addControlButtons(rowCountButtonX, currentY, ClickRegion::LAP_LOG_ROW_COUNT_DOWN, ClickRegion::LAP_LOG_ROW_COUNT_UP, m_lapLog);

                currentY += dim.lineHeightNormal;
            }

            // Right column data toggles (3 items)
            addDataCheckbox("Lap #", &m_lapLog->m_enabledColumns, LapLogHud::COL_LAP, false, m_lapLog, dataStartY);
            addGroupCheckbox("Sectors", &m_lapLog->m_enabledColumns,
                LapLogHud::COL_S1 | LapLogHud::COL_S2 | LapLogHud::COL_S3,
                false, m_lapLog, dataStartY + dim.lineHeightNormal);
            addDataCheckbox("Time", &m_lapLog->m_enabledColumns, LapLogHud::COL_TIME, false, m_lapLog, dataStartY + dim.lineHeightNormal * 2);
            break;

        case TAB_SESSION_BEST:
            activeHud = m_sessionBest;
            dataStartY = addHudControls(m_sessionBest);
            // Right column data toggles (2 items)
            addGroupCheckbox("Sectors", &m_sessionBest->m_enabledRows,
                SessionBestHud::ROW_S1 | SessionBestHud::ROW_S2 | SessionBestHud::ROW_S3,
                false, m_sessionBest, dataStartY);
            addGroupCheckbox("Laps", &m_sessionBest->m_enabledRows,
                SessionBestHud::ROW_LAST | SessionBestHud::ROW_BEST | SessionBestHud::ROW_IDEAL,
                false, m_sessionBest, dataStartY + dim.lineHeightNormal);
            break;

        case TAB_TELEMETRY:
            activeHud = m_telemetry;
            dataStartY = addHudControls(m_telemetry);

            // Display mode control (with cycle buttons)
            addDisplayModeControl(leftColumnX, currentY, dim, &m_telemetry->m_displayMode, m_telemetry);

            // Right column data toggles (8 items: Throttle, Front Brake, Rear Brake, Clutch, RPM, Front Susp, Rear Susp, Gear)
            addDataCheckbox("Throttle", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_THROTTLE, false, m_telemetry, dataStartY);
            addDataCheckbox("Front Brake", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_FRONT_BRAKE, false, m_telemetry, dataStartY + dim.lineHeightNormal);
            addDataCheckbox("Rear Brake", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_REAR_BRAKE, false, m_telemetry, dataStartY + dim.lineHeightNormal * 2);
            addDataCheckbox("Clutch", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_CLUTCH, false, m_telemetry, dataStartY + dim.lineHeightNormal * 3);
            addDataCheckbox("RPM", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_RPM, false, m_telemetry, dataStartY + dim.lineHeightNormal * 4);
            addDataCheckbox("Front Susp", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_FRONT_SUSP, false, m_telemetry, dataStartY + dim.lineHeightNormal * 5);
            addDataCheckbox("Rear Susp", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_REAR_SUSP, false, m_telemetry, dataStartY + dim.lineHeightNormal * 6);
            addDataCheckbox("Gear", &m_telemetry->m_enabledElements, TelemetryHud::ELEM_GEAR, false, m_telemetry, dataStartY + dim.lineHeightNormal * 7);
            break;

        case TAB_INPUT:
            activeHud = m_input;
            dataStartY = addHudControls(m_input);
            // Right column data toggles (3 items)
            addDataCheckbox("Crosshairs", &m_input->m_enabledElements, InputHud::ELEM_CROSSHAIRS, false, m_input, dataStartY);
            addDataCheckbox("Stick Trails", &m_input->m_enabledElements, InputHud::ELEM_TRAILS, false, m_input, dataStartY + dim.lineHeightNormal);
            addDataCheckbox("Numeric Values", &m_input->m_enabledElements, InputHud::ELEM_VALUES, false, m_input, dataStartY + dim.lineHeightNormal * 2);
            break;

        case TAB_PERFORMANCE:
            activeHud = m_performance;
            dataStartY = addHudControls(m_performance);

            // Display mode control (with cycle buttons)
            addDisplayModeControl(leftColumnX, currentY, dim, &m_performance->m_displayMode, m_performance);

            // Right column data toggles (2 items: FPS and CPU metrics)
            addDataCheckbox("FPS", &m_performance->m_enabledElements, PerformanceHud::ELEM_FPS, false, m_performance, dataStartY);
            addDataCheckbox("CPU", &m_performance->m_enabledElements, PerformanceHud::ELEM_CPU, false, m_performance, dataStartY + dim.lineHeightNormal);
            break;

        case TAB_PITBOARD:
            activeHud = m_pitboard;
            dataStartY = addHudControls(m_pitboard, false);  // No title support

            // Display mode control (left column, below scale)
            {
                // Determine display mode text (Always/Pit/Splits)
                const char* displayModeText = "";
                if (m_pitboard->m_displayMode == PitboardHud::MODE_ALWAYS) {
                    displayModeText = "Always";
                } else if (m_pitboard->m_displayMode == PitboardHud::MODE_PIT) {
                    displayModeText = "Pit";
                } else if (m_pitboard->m_displayMode == PitboardHud::MODE_SPLITS) {
                    displayModeText = "Splits";
                }

                char displayModeLabel[32];
                snprintf(displayModeLabel, sizeof(displayModeLabel), "Show: %s", displayModeText);
                addString(displayModeLabel, leftColumnX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                float displayModeButtonX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
                float minusX = displayModeButtonX;
                float plusX = displayModeButtonX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_BUTTON_GAP, dim.fontSize);
                float buttonWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::BUTTON_WIDTH, dim.fontSize);

                addString("[-]", minusX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
                addClickRegion(ClickRegion::DISPLAY_MODE_DOWN, minusX, currentY, buttonWidth, dim.lineHeightNormal,
                               m_pitboard, nullptr, &m_pitboard->m_displayMode, 0, false, 0);

                addString("[+]", plusX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
                addClickRegion(ClickRegion::DISPLAY_MODE_UP, plusX, currentY, buttonWidth, dim.lineHeightNormal,
                               m_pitboard, nullptr, &m_pitboard->m_displayMode, 0, false, 0);

                currentY += dim.lineHeightNormal;
            }

            // Right column data toggles (7 items)
            addDataCheckbox("Rider", &m_pitboard->m_enabledRows, PitboardHud::ROW_RIDER_ID, false, m_pitboard, dataStartY);
            addDataCheckbox("Session", &m_pitboard->m_enabledRows, PitboardHud::ROW_SESSION, false, m_pitboard, dataStartY + dim.lineHeightNormal);
            addDataCheckbox("Position", &m_pitboard->m_enabledRows, PitboardHud::ROW_POSITION, false, m_pitboard, dataStartY + dim.lineHeightNormal * 2);
            addDataCheckbox("Time", &m_pitboard->m_enabledRows, PitboardHud::ROW_TIME, false, m_pitboard, dataStartY + dim.lineHeightNormal * 3);
            addDataCheckbox("Lap", &m_pitboard->m_enabledRows, PitboardHud::ROW_LAP, false, m_pitboard, dataStartY + dim.lineHeightNormal * 4);
            addDataCheckbox("Last Lap", &m_pitboard->m_enabledRows, PitboardHud::ROW_LAST_LAP, false, m_pitboard, dataStartY + dim.lineHeightNormal * 5);
            addDataCheckbox("Gap", &m_pitboard->m_enabledRows, PitboardHud::ROW_GAP, false, m_pitboard, dataStartY + dim.lineHeightNormal * 6);
            break;

        case TAB_WIDGETS:
            // Widgets tab - table format with header row
            {
                // Table header - columns must match addWidgetRow positions
                float nameX = contentAreaStartX;
                float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, dim.fontSize);
                float titleX = visX + PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize);
                float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
                float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(6, dim.fontSize);
                float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(14, dim.fontSize);

                addString("Widget", nameX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("Vis", visX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("Title", titleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("BgTex", bgTexX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("Opacity", opacityX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
                addString("Scale", scaleX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
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

            // Range control
            {
                float controlX = leftColumnX;
                char rangeText[32];
                snprintf(rangeText, sizeof(rangeText), "Range: %.0fm", m_radarHud->getRadarRange());
                addString(rangeText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                float rangeButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
                addControlButtons(rangeButtonX, currentY, ClickRegion::RADAR_RANGE_DOWN, ClickRegion::RADAR_RANGE_UP, m_radarHud);

                currentY += dim.lineHeightNormal;
            }

            // Alert distance control (when triangles light up)
            {
                float controlX = leftColumnX;
                char alertText[32];
                snprintf(alertText, sizeof(alertText), "Alert: %.0fm", m_radarHud->getAlertDistance());
                addString(alertText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                float alertButtonX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SCALE_LABEL_WIDTH, dim.fontSize);
                addControlButtons(alertButtonX, currentY, ClickRegion::RADAR_ALERT_DISTANCE_DOWN, ClickRegion::RADAR_ALERT_DISTANCE_UP, m_radarHud);

                currentY += dim.lineHeightNormal;
            }

            // Colorize riders toggle
            {
                float controlX = leftColumnX;
                float colorizeY = currentY;

                bool colorizeRiders = m_radarHud->getColorizeRiders();
                const char* colorizeCheckbox = colorizeRiders ? "[X]" : "[ ]";
                addString(colorizeCheckbox, controlX, colorizeY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);
                float colorizeLabelX = controlX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, dim.fontSize);
                addString("Colorize", colorizeLabelX, colorizeY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                m_clickRegions.push_back(ClickRegion(
                    controlX, colorizeY,
                    PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_LABEL_MEDIUM, dim.fontSize),
                    dim.lineHeightNormal,
                    ClickRegion::RADAR_COLORIZE_TOGGLE, m_radarHud, 0, false, 0
                ));

                currentY += dim.lineHeightNormal;
            }

            // Label mode control
            {
                float controlX = leftColumnX;
                char labelModeText[32];
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
                snprintf(labelModeText, sizeof(labelModeText), "Labels: %s", modeStr);
                addString(labelModeText, controlX, currentY, Justify::LEFT,
                    Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

                m_clickRegions.push_back(ClickRegion(
                    controlX, currentY,
                    PluginUtils::calculateMonospaceTextWidth(20, dim.fontSize),
                    dim.lineHeightNormal,
                    ClickRegion::RADAR_LABEL_MODE_CYCLE, m_radarHud, 0, false, 0
                ));

                currentY += dim.lineHeightNormal;
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
        Fonts::ROBOTO_MONO_BOLD, TextColors::PRIMARY, dim.fontSize);
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
        Fonts::ROBOTO_MONO_BOLD, TextColors::SECONDARY, dim.fontSize);
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
        float newWidth = mapHud->getTrackLineWidthMeters() + (increase ? 0.5f : -0.5f);
        mapHud->setTrackLineWidthMeters(newWidth);
        rebuildRenderData();
        DEBUG_INFO_F("MapHud track line width %s to %.1fm", increase ? "increased" : "decreased", newWidth);
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
