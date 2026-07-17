// ============================================================================
// hud/settings/settings_layout.cpp
// Implementation of shared layout context and helper methods
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/color_config.h"
#include "../../core/font_config.h"
#include "../../core/ui_config.h"
#include "../../core/asset_manager.h"
#include "../../core/input_manager.h"

using namespace PluginConstants;

// ASCII ellipsis for truncation (game font doesn't support UTF-8)
static const char* ELLIPSIS = "...";

// Standard width (in characters) of a control's value field. Value strings longer
// than this are truncated by formatValue(), so keep cycle/toggle value text within it.
static constexpr int STANDARD_VALUE_WIDTH = 10;

SettingsLayoutContext::SettingsLayoutContext(
    SettingsHud* _parent,
    const BaseHud::ScaledDimensions& dim,
    float _labelX,
    float _controlX,
    float _rightColumnX,
    float _contentAreaStartX,
    float _panelWidth,
    float _currentY
)
    : parent(_parent)
    , fontSize(dim.fontSize)
    , fontSizeLarge(dim.fontSizeLarge)
    , lineHeightNormal(dim.lineHeightNormal)
    , lineHeightLarge(dim.lineHeightLarge)
    , paddingH(dim.paddingH)
    , paddingV(dim.paddingV)
    , labelX(_labelX)
    , controlX(_controlX)
    , rightColumnX(_rightColumnX)
    , contentAreaStartX(_contentAreaStartX)
    , panelWidth(_panelWidth)
    , currentY(_currentY)
    , scale(dim.scale)
    , tooltipY(0.0f)
{
}

float SettingsLayoutContext::charWidth() const {
    return PluginUtils::calculateMonospaceTextWidth(1, fontSize);
}

std::string SettingsLayoutContext::formatValue(const char* value, int maxWidth, bool center) {
    std::string result(value);

    // Truncate with ellipsis if too long. Reserve 3 cells for the ellipsis so the
    // result stays within maxWidth (substr(0, maxWidth-1) + "..." would be maxWidth+2
    // chars, overflowing the field under the cycle-control '>' arrow). Mirrors
    // PluginUtils::fitText().
    if (static_cast<int>(result.length()) > maxWidth) {
        int keep = (maxWidth > 3) ? (maxWidth - 3) : 0;
        result.resize(keep);
        result += ELLIPSIS;
    }

    // Left-pad for centering if requested
    if (center && static_cast<int>(result.length()) < maxWidth) {
        int padding = (maxWidth - static_cast<int>(result.length())) / 2;
        result = std::string(padding, ' ') + result;
    }

    // Right-pad to fixed width
    while (static_cast<int>(result.length()) < maxWidth) {
        result += ' ';
    }

    return result;
}

void SettingsLayoutContext::addSectionHeader(const char* title) {
    parent->addString(title, labelX, currentY, Justify::LEFT,
        Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), fontSize);
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addTabTooltip(const char* tabId) {
    // Store tabId and Y position for later - tooltip will be rendered by settings_hud.cpp
    // This allows control tooltips to replace tab tooltip when hovering
    currentTabId = tabId ? tabId : "";
    tooltipY = currentY;  // Save Y position for rendering
    // Reserve space for 2 tooltip lines (rendered later in settings_hud.cpp)
    currentY += lineHeightNormal * 2;
    addSpacing(0.5f);  // Small gap before controls
}

void SettingsLayoutContext::addCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    SettingsHud::ClickRegion::Type downType,
    SettingsHud::ClickRegion::Type upType,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Add row-wide tooltip region if tooltipId is provided (for Phase 3 hover)
    if (tooltipId && tooltipId[0] != '\0') {
        // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
        float rowWidth = panelWidth - (labelX - contentAreaStartX);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && !isOff) ? colors.getPrimary() : colors.getMuted();

    // Left arrow "<" - always visible, muted when disabled, clickable only when enabled
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            downType, targetHud, 0, false, 0
        ));
    }
    currentX += cw * 2;

    // Value with fixed width (formatted, left-aligned for consistent positioning)
    std::string formattedValue = formatValue(value, valueWidth, false);  // left-align for consistency
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow " >" - always visible, muted when disabled, clickable only when enabled
    parent->addString(" >", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            upType, targetHud, 0, false, 0
        ));
    }

    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    const SettingsHud::CycleControl& control,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId,
    bool tooltipOnArrows
) {
    // Register the descriptor for this rebuild (m_cycleControls is cleared in
    // lockstep with m_clickRegions, so the index stays valid exactly as long as
    // the regions below do). Registered even when disabled (no arrow regions),
    // keeping index assignment deterministic.
    const int cycleIndex = static_cast<int>(parent->m_cycleControls.size());
    parent->m_cycleControls.push_back(control);

    // Emit the row via the legacy cycle control, then tag the arrow regions it
    // created with the descriptor index (and optionally the row tooltip, which
    // is what the old per-type tooltip fallback resolved to for these controls).
    const size_t firstRegion = parent->m_clickRegions.size();
    addCycleControl(label, value, valueWidth,
        SettingsHud::ClickRegion::CYCLE_DOWN,
        SettingsHud::ClickRegion::CYCLE_UP,
        targetHud, enabled, isOff, tooltipId);
    for (size_t r = firstRegion; r < parent->m_clickRegions.size(); ++r) {
        auto& region = parent->m_clickRegions[r];
        if (region.type == SettingsHud::ClickRegion::CYCLE_UP ||
            region.type == SettingsHud::ClickRegion::CYCLE_DOWN) {
            region.cycleIndex = cycleIndex;
            if (tooltipOnArrows && tooltipId) region.tooltipId = tooltipId;
        }
    }
}

void SettingsLayoutContext::addSteppedControl(
    const char* label,
    const char* value,
    int valueWidth,
    const SettingsHud::SteppedControl& control,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId,
    bool tooltipOnArrows
) {
    // Register the descriptor for this rebuild (m_steppedControls is cleared in
    // lockstep with m_clickRegions, so the index stays valid exactly as long as
    // the regions below do). Registered even when disabled (no arrow regions),
    // keeping index assignment deterministic.
    const int steppedIndex = static_cast<int>(parent->m_steppedControls.size());
    parent->m_steppedControls.push_back(control);

    // Emit the row via the standard cycle control, then tag the arrow regions it
    // created with the descriptor index (and optionally the row tooltip, which is
    // what the old per-type tooltip fallback resolved to for these controls).
    const size_t firstRegion = parent->m_clickRegions.size();
    addCycleControl(label, value, valueWidth,
        SettingsHud::ClickRegion::STEPPED_DOWN,
        SettingsHud::ClickRegion::STEPPED_UP,
        targetHud, enabled, isOff, tooltipId);
    for (size_t r = firstRegion; r < parent->m_clickRegions.size(); ++r) {
        auto& region = parent->m_clickRegions[r];
        if (region.type == SettingsHud::ClickRegion::STEPPED_UP ||
            region.type == SettingsHud::ClickRegion::STEPPED_DOWN) {
            region.steppedIndex = steppedIndex;
            if (tooltipOnArrows && tooltipId) region.tooltipId = tooltipId;
        }
    }
}

void SettingsLayoutContext::addToggleControl(
    const char* label,
    bool isOn,
    SettingsHud::ClickRegion::Type toggleType,
    BaseHud* targetHud,
    uint32_t* bitfield,
    uint32_t flag,
    bool enabled,
    const char* tooltipId,
    const char* valueOverride
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Add row-wide tooltip region if tooltipId is provided (for Phase 3 hover)
    if (tooltipId && tooltipId[0] != '\0') {
        // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
        float rowWidth = panelWidth - (labelX - contentAreaStartX);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

    // Use override value if provided, otherwise show On/Off
    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, VALUE_WIDTH, false);

    // Left arrow "<" - always visible, muted when disabled, clickable only when enabled
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        if (bitfield != nullptr) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, bitfield, flag, false, targetHud
            ));
        } else {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, targetHud
            ));
        }
    }
    currentX += cw * 2;

    // Value with fixed width
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - always visible, muted when disabled, clickable only when enabled
    parent->addString(" >", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        if (bitfield != nullptr) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, bitfield, flag, false, targetHud
            ));
        } else {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, targetHud
            ));
        }
    }

    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addToggleControl(
    const char* label,
    bool isOn,
    SettingsHud::ClickRegion::Type toggleType,
    BaseHud* targetHud,
    bool* boolPtr,
    bool enabled,
    const char* tooltipId,
    const char* valueOverride
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Add row-wide tooltip region if tooltipId is provided
    if (tooltipId && tooltipId[0] != '\0') {
        float rowWidth = panelWidth - (labelX - contentAreaStartX);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, VALUE_WIDTH, false);

    // Left arrow "<" - always visible, muted when disabled, clickable only when enabled
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            toggleType, boolPtr, targetHud
        ));
    }
    currentX += cw * 2;

    // Value
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - always visible, muted when disabled, clickable only when enabled
    parent->addString(" >", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            toggleType, boolPtr, targetHud
        ));
    }

    currentY += lineHeightNormal;
}

float SettingsLayoutContext::addStandardHudControls(BaseHud* hud, bool enableTitle) {
    // Save starting Y for right column (data toggles)
    float sectionStartY = currentY;
    ColorConfig& colors = ColorConfig::getInstance();
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = panelWidth - (labelX - contentAreaStartX);

    // Visibility toggle
    bool isVisible = hud->isVisible();
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.visible"
    ));
    parent->addString("Visible", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);
    // Use inline toggle (same line)
    {
        float toggleX = controlX;
        float cw = charWidth();
        unsigned long valueColor = isVisible ? colors.getPrimary() : colors.getMuted();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::HUD_TOGGLE, hud
        ));
        toggleX += cw * 2;

        std::string formattedVisible = formatValue(isVisible ? "On" : "Off", VALUE_WIDTH, false);
        parent->addString(formattedVisible.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::HUD_TOGGLE, hud
        ));
    }
    currentY += lineHeightNormal;

    // Title toggle (can be disabled/grayed out)
    bool showTitle = enableTitle ? hud->getShowTitle() : false;
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.title"
    ));
    parent->addString("Title", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enableTitle ? colors.getSecondary() : colors.getMuted(), fontSize);
    {
        float toggleX = controlX;
        float cw = charWidth();
        unsigned long valueColor = (enableTitle && showTitle) ? colors.getPrimary() : colors.getMuted();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        if (enableTitle) {
            parent->addString("<", toggleX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                toggleX, currentY, cw * 2, lineHeightNormal,
                SettingsHud::ClickRegion::TITLE_TOGGLE, hud
            ));
        }
        toggleX += cw * 2;

        std::string formattedTitle = formatValue(showTitle ? "On" : "Off", VALUE_WIDTH, false);
        parent->addString(formattedTitle.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        if (enableTitle) {
            parent->addString(" >", toggleX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                toggleX, currentY, cw * 2, lineHeightNormal,
                SettingsHud::ClickRegion::TITLE_TOGGLE, hud
            ));
        }
    }
    currentY += lineHeightNormal;

    // Background texture variant cycle (Off, 1, 2, ...)
    bool hasTextures = !hud->getAvailableTextureVariants().empty();
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.texture"
    ));
    parent->addString("Texture", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), hasTextures ? colors.getSecondary() : colors.getMuted(), fontSize);
    char textureValue[16];
    int variant = hud->getTextureVariant();
    if (!hasTextures || variant == 0) {
        snprintf(textureValue, sizeof(textureValue), "Off");
    } else {
        snprintf(textureValue, sizeof(textureValue), "%d", variant);
    }
    {
        float toggleX = controlX;
        float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        if (hasTextures) {
            parent->addString("<", toggleX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                toggleX, currentY, cw * 2, lineHeightNormal,
                SettingsHud::ClickRegion::TEXTURE_VARIANT_DOWN, hud, 0, false, 0
            ));
        }
        toggleX += cw * 2;

        std::string formattedTexture = formatValue(textureValue, VALUE_WIDTH, false);
        parent->addString(formattedTexture.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), hasTextures ? colors.getPrimary() : colors.getMuted(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        if (hasTextures) {
            parent->addString(" >", toggleX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                toggleX, currentY, cw * 2, lineHeightNormal,
                SettingsHud::ClickRegion::TEXTURE_VARIANT_UP, hud, 0, false, 0
            ));
        }
    }
    currentY += lineHeightNormal;

    // Background opacity controls
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.opacity"
    ));
    parent->addString("Opacity", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);
    char opacityValue[16];
    int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
    snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
    {
        float toggleX = controlX;
        float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN, hud, 0, false, 0
        ));
        toggleX += cw * 2;

        std::string formattedOpacity = formatValue(opacityValue, VALUE_WIDTH, false);
        parent->addString(formattedOpacity.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getPrimary(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP, hud, 0, false, 0
        ));
    }
    currentY += lineHeightNormal;

    // Scale controls
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.scale"
    ));
    parent->addString("Scale", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);
    char scaleValue[16];
    int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
    snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
    {
        float toggleX = controlX;
        float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::SCALE_DOWN, hud, 0, false, 0
        ));
        toggleX += cw * 2;

        std::string formattedScale = formatValue(scaleValue, VALUE_WIDTH, false);
        parent->addString(formattedScale.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getPrimary(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::SCALE_UP, hud, 0, false, 0
        ));
    }
    currentY += lineHeightNormal;

    return sectionStartY;
}

void SettingsLayoutContext::addDataToggle(
    const char* label,
    uint32_t* bitfield,
    uint32_t flag,
    bool isRequired,
    BaseHud* targetHud,
    float yPos,
    int labelWidth
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();
    bool isChecked = (*bitfield & flag) != 0;
    bool enabled = !isRequired;

    // Label with padding
    char paddedLabel[32];
    snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
    parent->addString(paddedLabel, rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    // Toggle control
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(labelWidth, fontSize);
    unsigned long valueColor = (enabled && isChecked) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = 3;

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, flag, false, targetHud
        ));
    }
    toggleX += cw * 2;

    std::string formattedValue = formatValue(isChecked ? "On" : "Off", VALUE_WIDTH, false);
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, flag, false, targetHud
        ));
    }
}

void SettingsLayoutContext::addGroupToggle(
    const char* label,
    uint32_t* bitfield,
    uint32_t groupFlags,
    bool isRequired,
    BaseHud* targetHud,
    float yPos,
    int labelWidth
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();
    // Group is checked if all bits in group are set
    bool isChecked = (*bitfield & groupFlags) == groupFlags;
    bool enabled = !isRequired;

    // Label with padding
    char paddedLabel[32];
    snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
    parent->addString(paddedLabel, rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    // Toggle control
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(labelWidth, fontSize);
    unsigned long valueColor = (enabled && isChecked) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = 3;

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, groupFlags, false, targetHud
        ));
    }
    toggleX += cw * 2;

    std::string formattedValue = formatValue(isChecked ? "On" : "Off", VALUE_WIDTH, false);
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, groupFlags, false, targetHud
        ));
    }
}

void SettingsLayoutContext::nextLine() {
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addSpacing(float multiplier) {
    currentY += lineHeightNormal * multiplier;
}

float SettingsLayoutContext::addRightColumnCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    SettingsHud::ClickRegion::Type downType,
    SettingsHud::ClickRegion::Type upType,
    BaseHud* targetHud,
    float yPos,
    int labelWidth,
    bool enabled,
    bool isOff
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Label with padding
    char paddedLabel[32];
    snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
    parent->addString(paddedLabel, rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    // Cycle control
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(labelWidth, fontSize);
    unsigned long valueColor = (enabled && !isOff) ? colors.getPrimary() : colors.getMuted();

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            downType, targetHud, 0, false, 0
        ));
    }
    toggleX += cw * 2;

    // Value with fixed width (formatted, left-aligned for consistent positioning)
    std::string formattedValue = formatValue(value, valueWidth, false);  // left-align for consistency
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            upType, targetHud, 0, false, 0
        ));
    }

    return yPos + lineHeightNormal;
}

void SettingsLayoutContext::addWidgetRow(
    const char* name,
    BaseHud* hud,
    bool enableVisibility,
    bool enableTitle,
    bool enableBgTexture,
    bool enableOpacity,
    bool enableScale,
    const char* tooltipId,
    bool menuOnlyPointerRow
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Column positions (spacing for table layout with toggle controls)
    float nameX = labelX;
    float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, fontSize);   // After name
    float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, fontSize);   // After Vis toggle (< On >)
    float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, fontSize); // After Title toggle
    float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, fontSize); // After BG Tex toggle
    float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, fontSize); // After Opacity cycle

    // Add row-wide tooltip region if tooltipId is provided
    if (tooltipId && tooltipId[0] != '\0') {
        // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
        float rowWidth = panelWidth - (labelX - contentAreaStartX);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Widget name
    parent->addString(name, nameX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getPrimary(), fontSize);

    // Helper lambda for inline toggle control (position-based, no label)
    auto addInlineToggle = [&](float x, bool isOn, SettingsHud::ClickRegion::Type toggleType, bool enabled) {
        float currentX = x;
        unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
        constexpr int VALUE_WIDTH = 3;

        if (enabled) {
            parent->addString("<", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, hud
            ));
        }
        currentX += cw * 2;

        std::string formattedValue = formatValue(isOn ? "On" : "Off", VALUE_WIDTH, false);
        parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        if (enabled) {
            parent->addString(" >", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, hud
            ));
        }
    };

    // Helper lambda for inline cycle control (position-based, no label)
    auto addInlineCycle = [&](float x, const char* value, int valueWidth,
                              SettingsHud::ClickRegion::Type downType,
                              SettingsHud::ClickRegion::Type upType,
                              bool enabled) {
        float currentX = x;
        unsigned long valueColor = enabled ? colors.getPrimary() : colors.getMuted();

        if (enabled) {
            parent->addString("<", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                downType, hud, 0, false, 0
            ));
        }
        currentX += cw * 2;

        std::string formattedValue = formatValue(value, valueWidth, false);
        parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

        if (enabled) {
            parent->addString(" >", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                upType, hud, 0, false, 0
            ));
        }
    };

    // Visibility toggle (shows actual value, grayed out when disabled). The pointer
    // row is special: its toggle drives the menu-only-cursor mode, not the widget's
    // real visibility (On = pointer summoned by mouse movement during play; Off =
    // menu-only). The pointer's m_bVisible must stay true so it can still draw in the
    // settings menu, so it can't be the toggle target.
    if (menuOnlyPointerRow) {
        bool pointerOn = !UiConfig::getInstance().getMenuOnlyCursor();
        addInlineToggle(visX, pointerOn, SettingsHud::ClickRegion::MENU_ONLY_CURSOR_TOGGLE, true);
    } else {
        // Show the ACTIVE surface's visibility, like the per-HUD tabs: on the companion
        // window the toggle already edits the companion instance (HUD_TOGGLE routes by
        // active surface), so the displayed On/Off must read it too — otherwise a widget
        // enabled only on the companion still shows the game's state.
        bool companionSurface =
            InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
        bool visOn = companionSurface ? hud->getCompanionVisible() : hud->isVisible();
        addInlineToggle(visX, visOn, SettingsHud::ClickRegion::HUD_TOGGLE, enableVisibility);
    }

    // Title toggle - when disabled the widget never renders a title, so show the
    // effective state (Off) rather than echoing a possibly-stale persisted value
    addInlineToggle(titleX, enableTitle && hud->getShowTitle(), SettingsHud::ClickRegion::TITLE_TOGGLE, enableTitle);

    // BG Texture variant cycle (disabled if no textures available)
    bool hasTextures = !hud->getAvailableTextureVariants().empty();
    char texValue[8];
    int texVariant = hud->getTextureVariant();
    snprintf(texValue, sizeof(texValue), (!hasTextures || texVariant == 0) ? "Off" : "%d", texVariant);
    addInlineCycle(bgTexX, texValue, 3,
        SettingsHud::ClickRegion::TEXTURE_VARIANT_DOWN,
        SettingsHud::ClickRegion::TEXTURE_VARIANT_UP,
        enableBgTexture && hasTextures);

    // BG Opacity (shows muted value without arrows when disabled)
    char opacityValue[16];
    int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
    snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
    addInlineCycle(opacityX, opacityValue, 4,
        SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN,
        SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP,
        enableOpacity);

    // Scale (shows muted value without arrows when disabled)
    char scaleValue[16];
    int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
    snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
    addInlineCycle(scaleX, scaleValue, 4,
        SettingsHud::ClickRegion::SCALE_DOWN,
        SettingsHud::ClickRegion::SCALE_UP,
        enableScale);

    currentY += lineHeightNormal;
}

// Get icon display name from shape index (0 = Off)
std::string getShapeDisplayName(int shapeIndex, int maxWidth) {
    if (shapeIndex <= 0) return "Off";
    const auto& assetMgr = AssetManager::getInstance();
    int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
    std::string name = assetMgr.getIconDisplayName(spriteIndex);
    if (name.empty()) return "Unknown";
    if (static_cast<int>(name.length()) > maxWidth) name.resize(maxWidth);
    return name;
}
