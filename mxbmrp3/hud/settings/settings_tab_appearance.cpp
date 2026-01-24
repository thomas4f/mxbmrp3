// ============================================================================
// hud/settings/settings_tab_appearance.cpp
// Tab renderer for Appearance settings (fonts and colors)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/font_config.h"
#include "../../core/color_config.h"
#include "../../core/hud_manager.h"

using namespace PluginConstants;

// Static member function of SettingsHud - handles click events for Appearance tab
bool SettingsHud::handleClickTabAppearance(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::COLOR_CYCLE_NEXT:
        case ClickRegion::COLOR_CYCLE_PREV:
            {
                auto* colorSlotPtr = std::get_if<ColorSlot>(&region.targetPointer);
                if (!colorSlotPtr) return false;

                bool forward = (region.type == ClickRegion::COLOR_CYCLE_NEXT);
                ColorConfig::getInstance().cycleColor(*colorSlotPtr, forward);
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::FONT_CATEGORY_NEXT:
        case ClickRegion::FONT_CATEGORY_PREV:
            {
                auto* category = std::get_if<FontCategory>(&region.targetPointer);
                if (!category) return false;

                bool forward = (region.type == ClickRegion::FONT_CATEGORY_NEXT);
                FontConfig::getInstance().cycleFont(*category, forward);
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::DROP_SHADOW_TOGGLE:
            {
                ColorConfig& colorConfig = ColorConfig::getInstance();
                colorConfig.setDropShadow(!colorConfig.getDropShadow());
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabAppearance(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("appearance");

    FontConfig& fontConfig = FontConfig::getInstance();
    ColorConfig& colorConfig = ColorConfig::getInstance();
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);

    // === FONTS SECTION ===
    ctx.addSectionHeader("Fonts");

    // Helper lambda to add a font category row with cycle buttons
    auto addFontRow = [&](FontCategory category, const char* tooltipId) {
        const char* categoryName = FontConfig::getCategoryName(category);
        const char* fontDisplayName = fontConfig.getFontDisplayName(category);

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, tooltipId
        ));

        // Category name label
        ctx.parent->addString(categoryName, ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Font name with cycle arrows
        float cycleX = ctx.labelX + PluginUtils::calculateMonospaceTextWidth(12, ctx.fontSize);

        // Left arrow "<" with click region for PREV
        ctx.parent->addString("<", cycleX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            cycleX, ctx.currentY, charWidth * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::FONT_CATEGORY_PREV, category
        ));
        cycleX += charWidth * 2;

        // Font name (no click region)
        ctx.parent->addString(fontDisplayName, cycleX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        cycleX += charWidth * 22;  // Max font display name width

        // Right arrow ">" with click region for NEXT
        ctx.parent->addString(" >", cycleX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            cycleX, ctx.currentY, charWidth * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::FONT_CATEGORY_NEXT, category
        ));

        ctx.currentY += ctx.lineHeightNormal;
    };

    // All font categories
    addFontRow(FontCategory::TITLE, "appearance.font_title");
    addFontRow(FontCategory::NORMAL, "appearance.font_normal");
    addFontRow(FontCategory::STRONG, "appearance.font_strong");
    addFontRow(FontCategory::DIGITS, "appearance.font_digits");
    addFontRow(FontCategory::MARKER, "appearance.font_marker");
    addFontRow(FontCategory::SMALL, "appearance.font_small");

    // === COLORS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Colors");

    // Helper lambda to add a color row with preview and cycle buttons
    auto addColorRow = [&](ColorSlot slot, const char* tooltipId) {
        const char* slotName = ColorConfig::getSlotName(slot);
        unsigned long color = colorConfig.getColor(slot);
        const char* colorName = ColorPalette::getColorName(color);

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, tooltipId
        ));

        // Slot name label
        ctx.parent->addString(slotName, ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Color preview quad (small square showing the actual color)
        float previewX = ctx.labelX + PluginUtils::calculateMonospaceTextWidth(12, ctx.fontSize);
        float previewSize = ctx.lineHeightNormal * 0.8f;
        {
            SPluginQuad_t previewQuad;
            float quadX = previewX;
            float quadY = ctx.currentY + ctx.lineHeightNormal * 0.1f;
            ctx.parent->applyOffset(quadX, quadY);
            ctx.parent->setQuadPositions(previewQuad, quadX, quadY, previewSize, previewSize);
            previewQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            previewQuad.m_ulColor = color;
            ctx.parent->m_quads.push_back(previewQuad);
        }

        // Color name with cycle arrows (following addCycleControl pattern)
        float cycleX = previewX + previewSize + PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);

        // Left arrow "<" with click region for PREV
        ctx.parent->addString("<", cycleX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            cycleX, ctx.currentY, charWidth * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::COLOR_CYCLE_PREV, slot
        ));
        cycleX += charWidth * 2;

        // Color name (no click region)
        ctx.parent->addString(colorName, cycleX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        cycleX += charWidth * 10;  // Max color name width

        // Right arrow ">" with click region for NEXT
        ctx.parent->addString(" >", cycleX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            cycleX, ctx.currentY, charWidth * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::COLOR_CYCLE_NEXT, slot
        ));

        ctx.currentY += ctx.lineHeightNormal;
    };

    // All color slots
    addColorRow(ColorSlot::PRIMARY, "appearance.color_primary");
    addColorRow(ColorSlot::SECONDARY, "appearance.color_secondary");
    addColorRow(ColorSlot::TERTIARY, "appearance.color_tertiary");
    addColorRow(ColorSlot::MUTED, "appearance.color_muted");
    addColorRow(ColorSlot::BACKGROUND, "appearance.color_background");
    addColorRow(ColorSlot::ACCENT, "appearance.color_accent");
    addColorRow(ColorSlot::POSITIVE, "appearance.color_positive");
    addColorRow(ColorSlot::NEUTRAL, "appearance.color_neutral");
    addColorRow(ColorSlot::WARNING, "appearance.color_warning");
    addColorRow(ColorSlot::NEGATIVE, "appearance.color_negative");

    // === TEXT EFFECTS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Text Effects");

    // Drop shadow toggle
    ctx.addToggleControl("Drop shadow", colorConfig.getDropShadow(),
        SettingsHud::ClickRegion::DROP_SHADOW_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.drop_shadow");

    // No active HUD for appearance settings
    return nullptr;
}
