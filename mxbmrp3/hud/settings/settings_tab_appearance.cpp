// ============================================================================
// hud/settings/settings_tab_appearance.cpp
// Tab renderer for Appearance settings (fonts and colors)
// ============================================================================
#include <cstring>
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

        case ClickRegion::THEME_PREV:
        case ClickRegion::THEME_NEXT:
            {
                // The cycle is [None, <each discovered theme>]. Stored by NAME, so
                // the index is only ever a transient position in this list -- adding
                // or removing a theme folder can never repoint a saved setting.
                const auto& themes = AssetManager::getInstance().getThemes();
                const int count = static_cast<int>(themes.size()) + 1;   // +1 for None
                const std::string& cur = UiConfig::getInstance().getThemeName();
                int index = 0;
                for (size_t i = 0; i < themes.size(); ++i) {
                    if (themes[i].name == cur) { index = static_cast<int>(i) + 1; break; }
                }
                index += (region.type == ClickRegion::THEME_NEXT) ? 1 : -1;
                if (index < 0) index = count - 1;
                if (index >= count) index = 0;
                UiConfig::getInstance().setThemeName(
                    index == 0 ? std::string() : themes[static_cast<size_t>(index) - 1].name);
                // Every HUD's background geometry changes (1 quad <-> 9), so this is
                // a full rebuild, not a reposition.
                HudManager::getInstance().markAllHudsDirty();
                // ...and then re-validate, because a theme does not only redraw a
                // panel, it RESIZES it: the frame clearance is added to every panel's
                // padding, so switching from a 1-cell frame to an 8-cell one grows
                // every HUD by fourteen cells of width. Anything sitting flush against
                // the right or bottom edge grows straight off the display and stays
                // there -- validateAllHudPositions() otherwise only runs on a cursor
                // or window transition, and switching theme happens with the cursor
                // already up, so nothing fires until the menu is closed and reopened.
                // Found playing theme designer; the settings button widget is the one
                // that stings, since it is how you get back to this tab.
                // REQUEST, never call directly: we are inside a click handler, and
                // validateAllHudPositions() calls update() on every dirty HUD --
                // including this one, whose update() re-reads the same still-true
                // click edge and dispatches this handler again. That recursed into a
                // stack overflow. HudManager flushes the request once the frame's
                // update pass is over.
                HudManager::getInstance().requestPositionValidation();
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
    // Standard value width for the unit/format cycle controls (matches the General tab)
    constexpr int VALUE_WIDTH = 10;

    // === DISPLAY SECTION ===
    // Shown first so the theme and the units/format sit at the top of the tab.
    ctx.addSectionHeading("Display");

    // Panel theme: None + every theme discovered under mxbmrp3_data\themes\.
    // Hidden entirely when no themes are installed -- a cycler with one option is
    // just noise, and the folder is optional (users can delete it).
    if (AssetManager::getInstance().getThemeCount() > 0) {
        const std::string& themeName = UiConfig::getInstance().getThemeName();
        const ThemeAsset* theme = themeName.empty()
            ? nullptr : AssetManager::getInstance().getThemeByName(themeName);
        // An unknown saved name reads as "None", matching what actually renders.
        const std::string valueLabel = theme ? theme->displayName : std::string("None");
        ctx.addCycleControl("Panel Theme", valueLabel.c_str(), VALUE_WIDTH,
                            SettingsHud::ClickRegion::THEME_PREV,
                            SettingsHud::ClickRegion::THEME_NEXT,
                            /*targetHud=*/nullptr, /*enabled=*/true, /*isOff=*/false,
                            "appearance.theme");
    }

    // Speed unit toggle
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();
        ctx.addCycleControl("Speed Unit", (speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH) ? "km/h" : "mph", VALUE_WIDTH,
                            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE,
                            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE,
                            speedWidget, /*enabled=*/true, /*isOff=*/false,
                            "appearance.speed_unit");
    }

    // Fuel unit toggle
    {
        FuelWidget* fuelWidget = ctx.parent->getFuelWidget();
        ctx.addCycleControl("Fuel Unit", (fuelWidget && fuelWidget->getFuelUnit() == FuelWidget::FuelUnit::GALLONS) ? "gal" : "L", VALUE_WIDTH,
                            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE,
                            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE,
                            fuelWidget, /*enabled=*/true, /*isOff=*/false,
                            "appearance.fuel_unit");
    }

    // Temperature unit toggle
    {
        ctx.addCycleControl("Temp Unit", (UiConfig::getInstance().getTemperatureUnit() == TemperatureUnit::FAHRENHEIT) ? "F" : "C", VALUE_WIDTH,
                            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE,
                            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE,
                            nullptr, /*enabled=*/true, /*isOff=*/false,
                            "appearance.temp_unit");
    }

    // Clock format toggle
    {
        ClockWidget* clockWidget = ctx.parent->getClockWidget();
        ctx.addCycleControl("Clock Format", (clockWidget && clockWidget->getFormat24h()) ? "24h" : "12h", VALUE_WIDTH,
                            SettingsHud::ClickRegion::CLOCK_FORMAT_TOGGLE,
                            SettingsHud::ClickRegion::CLOCK_FORMAT_TOGGLE,
                            clockWidget, /*enabled=*/true, /*isOff=*/false,
                            "appearance.clock_format");
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

    // HUD display target: In-game / Companion (standalone window) / Both. LAST
    // control in the section: it is the one setting here that moves the whole UI to
    // another window, so it reads as the section's conclusion rather than its opening
    // question -- and Panel Theme, which people actually come to this tab for, gets
    // the top. A < value > cycler with friendly labels; opens/closes the companion
    // window on change.
    {
        DisplayTarget target = UiConfig::getInstance().getDisplayTarget();
        const char* valueLabel = (target == DisplayTarget::COMPANION) ? "Companion"
                               : (target == DisplayTarget::BOTH)      ? "Both"
                                                                      : "In-game";
        ctx.addCycleControl("HUD Display", valueLabel, VALUE_WIDTH,
                            SettingsHud::ClickRegion::DISPLAY_TARGET_TOGGLE,
                            SettingsHud::ClickRegion::DISPLAY_TARGET_TOGGLE,
                            /*targetHud=*/nullptr, /*enabled=*/true, /*isOff=*/false,
                            "appearance.display_target");
    }


    // (Grid Snap / Screen Clamp placement toggles live on the General tab's
    // Behavior section; still persisted under [Display].)

    // === FONTS SECTION ===
    ctx.addSectionHeading("Fonts");

    // Helper lambda to add a font category row with cycle buttons
    auto addFontRow = [&](FontCategory category, const char* tooltipId) {
        const char* categoryName = FontConfig::getCategoryName(category);
        const char* fontDisplayName = fontConfig.getFontDisplayName(category);

        // "Default" when the face IS the default, else its name.
        //
        // VALUE EQUALITY, not the override flag. The flag answers "did the user touch
        // this", which is not the question: cycle all the way round back to the
        // default face and the flag says "mine" while the screen is identical to
        // untouched. "Default" means the ACTIVE THEME's font where the theme supplies
        // one, else the built-in -- what you get by not touching it.
        const bool isDefaultFont =
            std::strcmp(fontConfig.getFontName(category),
                        FontConfig::getThemeOrDefaultFontName(category)) == 0;
        // The value is TRUNCATED to the field by the shared helper, like every other
        // row. This used to draw the raw name after the field shrank from 22
        // characters to STANDARD_VALUE_WIDTH, and the shipped names are 13-21
        // characters ("RobotoMono-Regular" renders as "Roboto Mono Regular"), so the
        // name ran straight through the ">" arrow beside it.
        //
        // NOTHING AFTER THE CONTROL. "Default" is simply one of the values the cycler
        // steps through, and the row reads as every other row on the tab. It used to
        // trail the resolved name in muted parentheses -- "Default  (Roboto Mono
        // Regular)" -- which answered a question ("which font IS the default") that the
        // row was not asking, put a second column of text on six rows that no other
        // control has, and needed its own truncation maths to stay inside the panel.
        ctx.addCycleControl(categoryName, isDefaultFont ? "Default" : fontDisplayName,
                            STANDARD_VALUE_WIDTH,
                            SettingsHud::ClickRegion::FONT_CATEGORY_PREV,
                            SettingsHud::ClickRegion::FONT_CATEGORY_NEXT,
                            category, tooltipId);
    };

    // All font categories
    addFontRow(FontCategory::TITLE, "appearance.font_title");
    addFontRow(FontCategory::NORMAL, "appearance.font_normal");
    addFontRow(FontCategory::STRONG, "appearance.font_strong");
    addFontRow(FontCategory::DIGITS, "appearance.font_digits");
    addFontRow(FontCategory::MARKER, "appearance.font_marker");
    addFontRow(FontCategory::SMALL, "appearance.font_small");

    // === COLORS SECTION ===
    ctx.addSectionHeading("Colors");

    // Helper lambda to add a color row with preview and cycle buttons
    auto addColorRow = [&](ColorSlot slot, const char* tooltipId) {
        const char* slotName = ColorConfig::getSlotName(slot);
        unsigned long color = colorConfig.getColor(slot);
        const char* colorName = ColorPalette::getColorName(color);

        // Colour swatch, between the label and the control column -- the control
        // itself starts at ctx.controlX like every other row's. Emitted BEFORE the
        // cycler, which is what advances ctx.currentY off this row.
        float previewX = ctx.labelX + PluginUtils::calculateMonospaceTextWidth(12, ctx.fontSize);
        float previewSize = ctx.lineHeightNormal * 0.8f;
        {
            SPluginQuad_t previewQuad;
            float quadX = previewX;
            float quadY = ctx.currentY + ctx.lineHeightNormal * 0.1f;
            ctx.parent->applyOffset(quadX, quadY);
            ctx.parent->setQuadPositions(previewQuad, quadX, quadY, previewSize, previewSize);
            // solid-quad-exempt: this IS the colour -- a swatch showing the palette
            // entry exactly, not a surface the entry is drawn on. A themed 9-slice
            // would tint it with the button's own art, and it would stop answering
            // the only question it is asked.
            previewQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            previewQuad.m_ulColor = color;
            ctx.parent->m_quads.push_back(previewQuad);
        }

        // "Default" when the colour IS the default -- see addFontRow for why this is
        // value equality rather than the override flag. No trailing "(Light Gray)"
        // either: "Default" is a value of the cycler, not a state annotated beside it.
        const bool isDefaultColor = (color == ColorConfig::getThemeOrDefaultColor(slot));
        ctx.addCycleControl(slotName, isDefaultColor ? "Default" : colorName,
                            STANDARD_VALUE_WIDTH,
                            SettingsHud::ClickRegion::COLOR_CYCLE_PREV,
                            SettingsHud::ClickRegion::COLOR_CYCLE_NEXT,
                            slot, tooltipId);
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
