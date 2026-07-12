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
#include "../../core/plugin_data.h"
#include "../../core/ui_config.h"
#include "../../core/companion_window.h"

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

        case ClickRegion::SHORT_TIME_FORMAT_TOGGLE:
            {
                PluginData& pd = PluginData::getInstance();
                pd.setShortTimeFormat(!pd.isShortTimeFormat());
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::DROP_SHADOW_TOGGLE:
            {
                UiConfig& uiConfig = UiConfig::getInstance();
                uiConfig.setDropShadow(!uiConfig.getDropShadow());
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::TITLE_ICONS_TOGGLE:
            {
                UiConfig& uiConfig = UiConfig::getInstance();
                uiConfig.setTitleIcons(!uiConfig.getTitleIcons());
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::DISPLAY_TARGET_TOGGLE:
            {
                // Cycle In-game -> Companion -> Both -> In-game, opening/closing the
                // companion window to match. In-game suppression is applied live in
                // HudManager::draw based on the target.
                DisplayTarget cur = UiConfig::getInstance().getDisplayTarget();
                DisplayTarget next = (cur == DisplayTarget::IN_GAME)   ? DisplayTarget::COMPANION
                                   : (cur == DisplayTarget::COMPANION) ? DisplayTarget::BOTH
                                                                       : DisplayTarget::IN_GAME;
                UiConfig::getInstance().setDisplayTarget(next);
                CompanionWindow::getInstance().setEnabled(next != DisplayTarget::IN_GAME);
                HudManager::getInstance().markAllHudsDirty();
                rebuildRenderData();
            }
            return true;

        // Display section unit toggles (moved here from the General tab).
        // CLOCK_FORMAT_TOGGLE is handled by the common handlers (works from any tab).
        case ClickRegion::SPEED_UNIT_TOGGLE:
            if (m_speed) {
                auto currentUnit = m_speed->getSpeedUnit();
                m_speed->setSpeedUnit(currentUnit == SpeedWidget::SpeedUnit::MPH
                    ? SpeedWidget::SpeedUnit::KMH
                    : SpeedWidget::SpeedUnit::MPH);
                setDataDirty();
            }
            return true;

        case ClickRegion::FUEL_UNIT_TOGGLE:
            if (m_fuel) {
                auto currentUnit = m_fuel->getFuelUnit();
                m_fuel->setFuelUnit(currentUnit == FuelWidget::FuelUnit::LITERS
                    ? FuelWidget::FuelUnit::GALLONS
                    : FuelWidget::FuelUnit::LITERS);
                setDataDirty();
            }
            return true;

        case ClickRegion::TEMP_UNIT_TOGGLE:
            {
                auto currentUnit = UiConfig::getInstance().getTemperatureUnit();
                UiConfig::getInstance().setTemperatureUnit(
                    currentUnit == TemperatureUnit::CELSIUS
                        ? TemperatureUnit::FAHRENHEIT
                        : TemperatureUnit::CELSIUS);
                // Also update SessionHud since it displays temperature
                if (m_session) {
                    m_session->setDataDirty();
                }
                setDataDirty();
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
    const float cw = charWidth;  // alias used by the Display unit/format rows below
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);
    // Standard value width for the unit/format cycle controls (matches the General tab)
    constexpr int VALUE_WIDTH = 10;

    // === DISPLAY SECTION ===
    // Shown first so units/format sit at the top of the Appearance tab.
    ctx.addSectionHeader("Display");

    // HUD display target: In-game / Companion (standalone window) / Both. First
    // control in the tab. A < value > cycler with friendly labels; opens/closes the
    // companion window on change.
    {
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "appearance.display_target"));

        ctx.parent->addString("HUD Display", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        DisplayTarget target = UiConfig::getInstance().getDisplayTarget();
        const char* valueLabel = (target == DisplayTarget::COMPANION) ? "Companion"
                               : (target == DisplayTarget::BOTH)      ? "Both"
                                                                      : "In-game";
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::DISPLAY_TARGET_TOGGLE, nullptr));
        currentX += cw * 2;

        std::string formattedValue = ctx.formatValue(valueLabel, VALUE_WIDTH, false);
        ctx.parent->addString(formattedValue.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::DISPLAY_TARGET_TOGGLE, nullptr));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Speed unit toggle
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "appearance.speed_unit"
        ));

        ctx.parent->addString("Speed Unit", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current unit with < > cycle pattern (arrows=accent, value=primary)
        bool isKmh = speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE, speedWidget
        ));
        currentX += cw * 2;

        // Left-align value within VALUE_WIDTH for consistent positioning
        std::string formattedValue = ctx.formatValue(isKmh ? "km/h" : "mph", VALUE_WIDTH, false);
        ctx.parent->addString(formattedValue.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE, speedWidget
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Fuel unit toggle
    {
        FuelWidget* fuelWidget = ctx.parent->getFuelWidget();

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "appearance.fuel_unit"
        ));

        ctx.parent->addString("Fuel Unit", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current unit with < > cycle pattern (arrows=accent, value=primary)
        bool isGallons = fuelWidget && fuelWidget->getFuelUnit() == FuelWidget::FuelUnit::GALLONS;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE, fuelWidget
        ));
        currentX += cw * 2;

        // Left-align value within VALUE_WIDTH for consistent positioning
        std::string formattedFuel = ctx.formatValue(isGallons ? "gal" : "L", VALUE_WIDTH, false);
        ctx.parent->addString(formattedFuel.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE, fuelWidget
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Temperature unit toggle
    {
        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "appearance.temp_unit"
        ));

        ctx.parent->addString("Temp Unit", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        // Display current unit with < > cycle pattern (arrows=accent, value=primary)
        bool isFahrenheit = UiConfig::getInstance().getTemperatureUnit() == TemperatureUnit::FAHRENHEIT;
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE, nullptr
        ));
        currentX += cw * 2;

        // Left-align value within VALUE_WIDTH for consistent positioning
        std::string formattedTemp = ctx.formatValue(isFahrenheit ? "F" : "C", VALUE_WIDTH, false);
        ctx.parent->addString(formattedTemp.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE, nullptr
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Clock format toggle
    {
        ClockWidget* clockWidget = ctx.parent->getClockWidget();

        // Add tooltip row
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "appearance.clock_format"
        ));

        ctx.parent->addString("Clock Format", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

        bool is24h = clockWidget && clockWidget->getFormat24h();
        float currentX = ctx.controlX;

        ctx.parent->addString("<", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::CLOCK_FORMAT_TOGGLE, clockWidget
        ));
        currentX += cw * 2;

        std::string formattedValue = ctx.formatValue(is24h ? "24h" : "12h", VALUE_WIDTH, false);
        ctx.parent->addString(formattedValue.c_str(), currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPrimary(), ctx.fontSize);
        currentX += cw * VALUE_WIDTH;

        ctx.parent->addString(" >", currentX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getAccent(), ctx.fontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
            SettingsHud::ClickRegion::CLOCK_FORMAT_TOGGLE, clockWidget
        ));

        ctx.currentY += ctx.lineHeightNormal;
    }

    // Compact time format toggle
    ctx.addToggleControl("Compact Times", PluginData::getInstance().isShortTimeFormat(),
        SettingsHud::ClickRegion::SHORT_TIME_FORMAT_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.compact_times");

    // Drop shadow toggle
    ctx.addToggleControl("Drop Shadow", UiConfig::getInstance().getDropShadow(),
        SettingsHud::ClickRegion::DROP_SHADOW_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.drop_shadow");

    // UI icons toggle (HUD title icons, settings tab/section icons, settings button)
    ctx.addToggleControl("UI Icons", UiConfig::getInstance().getTitleIcons(),
        SettingsHud::ClickRegion::TITLE_ICONS_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.hud_icons");

    // (Grid Snap / Screen Clamp placement toggles live on the General tab's
    // Behavior section; still persisted under [Display].)

    // === FONTS SECTION ===
    ctx.addSpacing(0.5f);
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

    // No active HUD for appearance settings
    return nullptr;
}
