// ============================================================================
// hud/settings/settings_layout.h
// Shared layout context and helper methods for settings panel rendering
// ============================================================================
#pragma once

#include "../base_hud.h"
#include "../settings_hud.h"
#include "../../core/color_config.h"
#include "../../core/font_config.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include <string>

// Forward declarations
class SettingsHud;
class BaseHud;

// Layout context for settings panel rendering
// Replaces lambda captures with explicit context object, enabling extraction of tab
// rendering into separate files while maintaining access to shared state.
struct SettingsLayoutContext {
    // Parent reference for adding render primitives
    SettingsHud* parent;

    // Dimensions (from getScaledDimensions())
    float fontSize;
    float fontSizeLarge;
    float lineHeightNormal;
    float lineHeightLarge;
    float paddingH;
    float paddingV;

    // Layout positions
    float labelX;              // Where labels start (left column)
    float controlX;            // Where control values start (toggle position)
    float rightColumnX;        // Where right column starts (for data toggles)
    float contentAreaStartX;   // Start of content area (after tab bar)
    float panelWidth;          // Content area width (from contentAreaStartX to right edge)

    // Mutable cursor
    float currentY;

    // Scale factor
    float scale;

    // Tab ID for tooltip display (set by addTabTooltip)
    std::string currentTabId;
    float tooltipY;  // Y position where tooltip should be rendered

    // Constructor
    SettingsLayoutContext(
        SettingsHud* _parent,
        const BaseHud::ScaledDimensions& dim,
        float _labelX,
        float _controlX,
        float _rightColumnX,
        float _contentAreaStartX,
        float _panelWidth,
        float _currentY
    );

    // === Layout Helper Methods ===

    // Add a section header (bold, primary color)
    void addSectionHeader(const char* title);

    // Add tab tooltip area from tooltips.json (if available)
    // tabId is the lowercase tab name (e.g., "standings", "map")
    void addTabTooltip(const char* tabId);

    // Add a cycle control with < value > pattern
    // If enabled is false, no click regions are added and muted color is used
    // If isOff is true, the value is muted (for "Off" state visual consistency)
    // tooltipId is optional - if provided, a row-wide hover region is created
    // displayMode is optional - if provided, passed to click handler for DISPLAY_MODE_* types
    void addCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        SettingsHud::ClickRegion::Type downType,
        SettingsHud::ClickRegion::Type upType,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr,
        uint8_t* displayMode = nullptr
    );

    // Add a toggle control with < On/Off > pattern
    // Both arrows trigger the same toggle action
    // tooltipId is optional - if provided, a row-wide hover region is created
    // valueOverride - if provided, shows this text instead of "On"/"Off"
    void addToggleControl(
        const char* label,
        bool isOn,
        SettingsHud::ClickRegion::Type toggleType,
        BaseHud* targetHud,
        uint32_t* bitfield = nullptr,
        uint32_t flag = 0,
        bool enabled = true,
        const char* tooltipId = nullptr,
        const char* valueOverride = nullptr
    );

    // Add standard HUD controls block (Visible, Title, Texture, Opacity, Scale)
    // Returns the Y position where the section started (for right column alignment)
    float addStandardHudControls(BaseHud* hud, bool enableTitle = true);

    // Add a data toggle control in the right column (for bitfield toggles)
    // labelWidth should accommodate the longest label in the group for alignment
    void addDataToggle(
        const char* label,
        uint32_t* bitfield,
        uint32_t flag,
        bool isRequired,
        BaseHud* targetHud,
        float yPos,
        int labelWidth = 12
    );

    // Add a group toggle control in the right column (toggles multiple bits)
    void addGroupToggle(
        const char* label,
        uint32_t* bitfield,
        uint32_t groupFlags,
        bool isRequired,
        BaseHud* targetHud,
        float yPos,
        int labelWidth = 12
    );

    // Add a cycle control in the right column (label + < value > on same row)
    // Used for Rows, Show mode, etc. in the right column area
    // Returns the Y position after this control
    float addRightColumnCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        SettingsHud::ClickRegion::Type downType,
        SettingsHud::ClickRegion::Type upType,
        BaseHud* targetHud,
        float yPos,
        int labelWidth = 12,
        bool enabled = true,
        bool isOff = false
    );

    // Add a display mode control (Graphs/Numbers/Both) in the right column
    // Used by TelemetryHud and PerformanceHud
    // Returns the Y position after this control
    float addDisplayModeControl(
        uint8_t* displayMode,
        BaseHud* targetHud,
        float yPos
    );

    // Advance cursor by one line
    void nextLine();

    // Add vertical spacing (multiplier of lineHeightNormal)
    void addSpacing(float multiplier = 0.5f);

    // Helper to format and truncate values for cycle controls
    // If value exceeds maxWidth, truncates to maxWidth-1 chars + ellipsis
    // If center is true, centers the value within maxWidth
    static std::string formatValue(const char* value, int maxWidth, bool center = false);

    // Add a widget row for the Widgets tab table
    // Layout: Name | Visible | Title | Texture | Opacity | Scale
    // tooltipId is optional - if provided, a row-wide hover region is created
    void addWidgetRow(
        const char* name,
        BaseHud* hud,
        bool enableTitle = true,
        bool enableOpacity = true,
        bool enableScale = true,
        bool enableVisibility = true,
        bool enableBgTexture = true,
        const char* tooltipId = nullptr
    );

private:
    // Helper to calculate character width at current scale
    float charWidth() const;
};

// Utility function for icon/shape display names
// Gets the display name for an icon shape index (0 = Off, 1-N = icon names)
std::string getShapeDisplayName(int shapeIndex, int maxWidth = 12);

// Note: Tab rendering functions are declared as static members of SettingsHud
// to inherit the friend relationships with HUD classes. See settings_hud.h.

