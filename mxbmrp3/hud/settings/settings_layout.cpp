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
#include "../../core/asset_manager.h"

using namespace PluginConstants;

// ASCII ellipsis for truncation (game font doesn't support UTF-8)
static const char* ELLIPSIS = "...";

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

    // Truncate with ellipsis if too long
    if (static_cast<int>(result.length()) > maxWidth) {
        result = result.substr(0, maxWidth - 1) + ELLIPSIS;
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
    const char* tooltipId,
    uint8_t* displayMode
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

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        if (displayMode) {
            // Use display mode constructor for DISPLAY_MODE_* types
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                downType, displayMode, targetHud
            ));
        } else {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                downType, targetHud, 0, false, 0
            ));
        }
    }
    currentX += cw * 2;  // "< " (spacing preserved even if arrow hidden)

    // Value with fixed width (formatted, left-aligned for consistent positioning)
    std::string formattedValue = formatValue(value, valueWidth, false);  // left-align for consistency
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        if (displayMode) {
            // Use display mode constructor for DISPLAY_MODE_* types
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                upType, displayMode, targetHud
            ));
        } else {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                upType, targetHud, 0, false, 0
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
    constexpr int VALUE_WIDTH = 10;  // Standard width for all controls (matches addCycleControl)

    // Use override value if provided, otherwise show On/Off
    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, VALUE_WIDTH, false);

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        if (bitfield != nullptr) {
            // CHECKBOX type with bitfield
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, bitfield, flag, false, targetHud
            ));
        } else {
            // Simple toggle without bitfield
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, targetHud
            ));
        }
    }
    currentX += cw * 2;  // "< " (spacing preserved even if arrow hidden)

    // Value with fixed width (centered)
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        if (bitfield != nullptr) {
            // CHECKBOX type with bitfield
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, bitfield, flag, false, targetHud
            ));
        } else {
            // Simple toggle without bitfield
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, targetHud
            ));
        }
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
        constexpr int VALUE_WIDTH = 10;  // Standard width for all controls

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
        constexpr int VALUE_WIDTH = 10;  // Standard width for all controls

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
        constexpr int VALUE_WIDTH = 10;  // Standard width for all controls

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
        constexpr int VALUE_WIDTH = 10;  // Standard width for all controls

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
        constexpr int VALUE_WIDTH = 10;  // Standard width for all controls

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

float SettingsLayoutContext::addDisplayModeControl(
    uint8_t* displayMode,
    BaseHud* targetHud,
    float yPos
) {
    ColorConfig& colors = ColorConfig::getInstance();

    // Determine display mode text
    const char* displayModeText = "";
    if (*displayMode == 0) {
        displayModeText = "Graphs";
    } else if (*displayMode == 1) {
        displayModeText = "Numbers";
    } else if (*displayMode == 2) {
        displayModeText = "Both";
    }

    // Render label
    parent->addString("Display", rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);

    // Cycle control position
    float cw = charWidth();
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(12, fontSize);
    constexpr int MAX_VALUE_WIDTH = 10;  // Standard width for all controls

    // Left arrow "<"
    parent->addString("<", toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), colors.getAccent(), fontSize);
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        toggleX, yPos, cw * 2, lineHeightNormal,
        SettingsHud::ClickRegion::DISPLAY_MODE_DOWN, displayMode, targetHud
    ));
    toggleX += cw * 2;

    // Value with fixed width (left-aligned for consistent positioning)
    std::string formattedValue = formatValue(displayModeText, MAX_VALUE_WIDTH, false);
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), colors.getPrimary(), fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(MAX_VALUE_WIDTH, fontSize);

    // Right arrow " >"
    parent->addString(" >", toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), colors.getAccent(), fontSize);
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        toggleX, yPos, cw * 2, lineHeightNormal,
        SettingsHud::ClickRegion::DISPLAY_MODE_UP, displayMode, targetHud
    ));

    return yPos + lineHeightNormal;
}

void SettingsLayoutContext::addWidgetRow(
    const char* name,
    BaseHud* hud,
    bool enableTitle,
    bool enableOpacity,
    bool enableScale,
    bool enableVisibility,
    bool enableBgTexture,
    const char* tooltipId
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

    // Visibility toggle (shows actual value, grayed out when disabled)
    addInlineToggle(visX, hud->isVisible(), SettingsHud::ClickRegion::HUD_TOGGLE, enableVisibility);

    // Title toggle (shows actual value, grayed out when disabled)
    addInlineToggle(titleX, hud->getShowTitle(), SettingsHud::ClickRegion::TITLE_TOGGLE, enableTitle);

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
