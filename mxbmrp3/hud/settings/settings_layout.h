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

    // Add tab tooltip area (string sourced from TooltipManager)
    // tabId is the lowercase tab name (e.g., "standings", "map")
    void addTabTooltip(const char* tabId);

    // Add a cycle control with < value > pattern
    // If enabled is false, no click regions are added and muted color is used
    // If isOff is true, the value is muted (for "Off" state visual consistency)
    // tooltipId is optional - if provided, a row-wide hover region is created
    void addCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        SettingsHud::ClickRegion::Type downType,
        SettingsHud::ClickRegion::Type upType,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr
    );

    // Add a cycle control whose arrows step a mod-N state through the shared
    // data-driven cycle handler (ClickRegion::CYCLE_UP/CYCLE_DOWN + a
    // CycleControl descriptor registered for this rebuild). Use this instead of
    // a dedicated enum pair when the handler would be the plain archetype
    // "value = (value ± 1) mod N; hud->setDataDirty(); setDataDirty();"
    // (optionally with uniform postStep work). Cycles never hold-accelerate.
    // tooltipOnArrows mirrors addSteppedControl: stamp tooltipId onto the two
    // arrow regions when the old per-type getTooltipIdForRegion fallback had an
    // entry for the control; pass false for controls whose arrows historically
    // showed no tooltip.
    void addCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        const SettingsHud::CycleControl& control,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr,
        bool tooltipOnArrows = true
    );

    // Add a cycle control whose arrows step a numeric HUD member through the
    // shared data-driven stepped handler (ClickRegion::STEPPED_UP/STEPPED_DOWN +
    // a SteppedControl descriptor registered for this rebuild). Use this instead
    // of a dedicated enum pair when the handler would be the plain archetype
    // "value = applyAccelerated*(...); hud->setDataDirty(); setDataDirty();".
    // Handlers with any other side effect keep their own enum pair.
    // tooltipOnArrows: also stamp tooltipId onto the two arrow regions (matches
    // the old per-type getTooltipIdForRegion fallback; pass false for controls
    // whose arrows historically showed no tooltip).
    void addSteppedControl(
        const char* label,
        const char* value,
        int valueWidth,
        const SettingsHud::SteppedControl& control,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr,
        bool tooltipOnArrows = true
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

    // Overload for bool* toggle controls (e.g., LAP_LOG_GAP_ROW_TOGGLE)
    void addToggleControl(
        const char* label,
        bool isOn,
        SettingsHud::ClickRegion::Type toggleType,
        BaseHud* targetHud,
        bool* boolPtr,
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

    // Advance cursor by one line
    void nextLine();

    // Add vertical spacing (multiplier of lineHeightNormal)
    void addSpacing(float multiplier = 0.5f);

    // Helper to format and truncate values for cycle controls
    // If value exceeds maxWidth, truncates to maxWidth-1 chars + ellipsis
    // If center is true, centers the value within maxWidth
    static std::string formatValue(const char* value, int maxWidth, bool center = false);

    // Add a widget row for the Widgets tab table
    // The enable* parameters follow the visual column order:
    //   Name | Visible | Title | Texture | Opacity | Scale
    // tooltipId is optional - if provided, a row-wide hover region is created
    void addWidgetRow(
        const char* name,
        BaseHud* hud,
        bool enableVisibility = true,
        bool enableTitle = true,
        bool enableBgTexture = true,
        bool enableOpacity = true,
        bool enableScale = true,
        const char* tooltipId = nullptr,
        // Pointer row only: the visibility column can't toggle the widget's real
        // m_bVisible (the pointer must stay drawable so it can still appear in the
        // settings menu), so it toggles the menu-only-cursor mode instead. On = the
        // pointer is summoned by mouse movement during play; Off = menu-only.
        bool menuOnlyPointerRow = false
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

