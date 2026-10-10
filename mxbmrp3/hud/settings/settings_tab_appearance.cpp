// ============================================================================
// hud/settings/settings_tab_appearance.cpp
// Tab renderer for Appearance settings (fonts and colors)
// ============================================================================
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../session_hud.h"
#include "../speed_widget.h"
#include "../fuel_widget.h"
#include "../clock_widget.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/font_config.h"
#include "../../core/asset_manager.h"
#include "../../core/color_config.h"
#include "../../core/hud_manager.h"
#include "../../core/plugin_data.h"
#include "../../core/ui_config.h"
#include "../../core/companion_window.h"
#include "../../core/system_messages.h"

using namespace PluginConstants;

// Static member function of SettingsHud - handles click events for Appearance tab
bool SettingsHud::handleClickTabAppearance(const ClickRegion& region) {
    switch (region.type) {
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

        // Display section unit toggles.
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
// A font category's list: FontConfig::cycleFont's ring, in its order -- Default
// (follows the theme; picking it un-pins the category), then every face but the
// emphasis companions.
static SettingsHud::CycleControl fontCycle(FontCategory category) {
    auto fonts = std::make_shared<std::vector<const FontAsset*>>();
    for (const FontAsset& f : AssetManager::getInstance().getFonts()) {
        if (!f.emphasisOnly) fonts->push_back(&f);
    }
    SettingsHud::CycleControl c;
    c.count = static_cast<int>(fonts->size()) + 1;
    c.get = [fonts, category]() {
        FontConfig& config = FontConfig::getInstance();
        if (!config.isOverridden(category)) return 0;
        const std::string current = config.getFontName(category);
        for (size_t i = 0; i < fonts->size(); ++i) {
            if ((*fonts)[i]->filename == current) return static_cast<int>(i) + 1;
        }
        return 0;
    };
    c.set = [fonts, category](int i) {
        if (i <= 0) FontConfig::getInstance().clearOverride(category);
        else FontConfig::getInstance().setFont(category, (*fonts)[static_cast<size_t>(i) - 1]->filename);
    };
    c.nameOf = [fonts](int i) {
        return i <= 0 ? std::string("Default") : (*fonts)[static_cast<size_t>(i) - 1]->displayName;
    };
    // Each name in its own face, so the list previews what picking it does.
    // fontIndex is the asset's position in getFonts() plus one (AssetManager).
    c.fontOf = [fonts, category](int i) {
        AssetManager& assets = AssetManager::getInstance();
        if (i <= 0) return assets.getFontIndexByName(FontConfig::getThemeOrDefaultFontName(category));
        return static_cast<int>((*fonts)[static_cast<size_t>(i) - 1] - assets.getFonts().data()) + 1;
    };
    // The arrows take FontConfig::cycleFont's own step (also what the tests pin).
    c.step = [category](bool forward) { FontConfig::getInstance().cycleFont(category, forward); };
    c.postStep = []() { HudManager::getInstance().markAllHudsDirty(); };
    return c;
}

// A colour slot's list: ColorConfig::cycleColor's ring -- Default (follows the
// theme; picking it un-pins the slot), then the palette. Each entry carries its swatch.
static SettingsHud::CycleControl colorCycle(ColorSlot slot) {
    SettingsHud::CycleControl c;
    c.count = static_cast<int>(ColorPalette::ALL_COLORS.size()) + 1;
    c.get = [slot]() {
        ColorConfig& config = ColorConfig::getInstance();
        if (!config.isOverridden(slot)) return 0;
        return ColorPalette::getColorIndex(config.getColor(slot)) + 1;   // off-palette reads as Default
    };
    c.set = [slot](int i) {
        if (i <= 0) ColorConfig::getInstance().clearOverride(slot);
        else ColorConfig::getInstance().setColor(slot, ColorPalette::ALL_COLORS[static_cast<size_t>(i) - 1]);
    };
    c.nameOf = [](int i) {
        return i <= 0 ? std::string("Default")
                      : std::string(ColorPalette::getColorName(ColorPalette::ALL_COLORS[static_cast<size_t>(i) - 1]));
    };
    c.swatchOf = [slot](int i) {
        return i <= 0 ? ColorConfig::getThemeOrDefaultColor(slot) : ColorPalette::ALL_COLORS[static_cast<size_t>(i) - 1];
    };
    c.step = [slot](bool forward) { ColorConfig::getInstance().cycleColor(slot, forward); };
    c.postStep = []() { HudManager::getInstance().markAllHudsDirty(); };
    return c;
}

BaseHud* SettingsHud::renderTabAppearance(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("appearance");

    FontConfig& fontConfig = FontConfig::getInstance();
    ColorConfig& colorConfig = ColorConfig::getInstance();

    // === DISPLAY SECTION ===
    // Shown first so the theme and the units/format sit at the top of the tab.
    ctx.addSectionHeading("Display");
    constexpr int DISPLAY_LABEL_CHARS = 14;   // "Compact times" and a space

    // Panel theme: None + every theme discovered under mxbmrp3_data\themes\.
    // Hidden entirely when no themes are installed -- a cycler with one option is
    // just noise, and the folder is optional (users can delete it).
    if (AssetManager::getInstance().getThemeCount() > 0) {
        const std::string& themeName = UiConfig::getInstance().getThemeName();
        const ThemeAsset* theme = themeName.empty()
            ? nullptr : AssetManager::getInstance().getThemeByName(themeName);
        // An unknown saved name reads as "None", matching what actually renders.
        const std::string valueLabel = theme ? theme->displayName : std::string("None");
        // [None, <each discovered theme>], stored by NAME, so a position here is
        // only ever transient -- adding or removing a theme folder can never
        // repoint a saved setting.
        SettingsHud::CycleControl themes;
        themes.count = static_cast<int>(AssetManager::getInstance().getThemes().size()) + 1;
        themes.get = []() {
            const auto& list = AssetManager::getInstance().getThemes();
            const std::string& cur = UiConfig::getInstance().getThemeName();
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i].name == cur) return static_cast<int>(i) + 1;
            }
            return 0;
        };
        themes.set = [](int i) {
            const auto& list = AssetManager::getInstance().getThemes();
            UiConfig::getInstance().setThemeName(i <= 0 ? std::string() : list[static_cast<size_t>(i) - 1].name);
        };
        themes.nameOf = [](int i) {
            return i <= 0 ? std::string("None")
                          : AssetManager::getInstance().getThemes()[static_cast<size_t>(i) - 1].displayName;
        };
        // Every HUD's background geometry changes (1 quad <-> 9), so a full
        // rebuild -- and a theme RESIZES every panel (its frame clearance joins the
        // padding), so anything flush against an edge would grow off the display:
        // REQUEST validation, never call it from inside a click (it updates this
        // HUD, which re-reads the click and recurses).
        themes.postStep = []() {
            HudManager::getInstance().markAllHudsDirty();
            HudManager::getInstance().requestPositionValidation();
        };
        themes.repeat = false;   // a step re-lays out every HUD
        // Full width for a theme's name, its arrows in the column of the cells below.
        ctx.setRowLabelChars(DISPLAY_LABEL_CHARS);
        ctx.addCycleControl("Panel theme", valueLabel.c_str(), themes,
                            /*targetHud=*/nullptr, /*enabled=*/true, /*isOff=*/false,
                            "appearance.theme");
        ctx.setRowLabelChars(0);
    }

    // The rest of Display side by side: short labels and short values.
    ctx.beginColumns(2, 10, DISPLAY_LABEL_CHARS);

    // Speed unit toggle
    {
        SpeedWidget* speedWidget = ctx.parent->getSpeedWidget();
        ctx.addCycleControl("Speed unit", (speedWidget && speedWidget->getSpeedUnit() == SpeedWidget::SpeedUnit::KMH) ? "km/h" : "mph",
                            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE,
                            SettingsHud::ClickRegion::SPEED_UNIT_TOGGLE,
                            speedWidget, /*enabled=*/true, /*isOff=*/false,
                            "appearance.speed_unit");
    }

    // Fuel unit toggle
    {
        FuelWidget* fuelWidget = ctx.parent->getFuelWidget();
        ctx.addCycleControl("Fuel unit", (fuelWidget && fuelWidget->getFuelUnit() == FuelWidget::FuelUnit::GALLONS) ? "gal" : "L",
                            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE,
                            SettingsHud::ClickRegion::FUEL_UNIT_TOGGLE,
                            fuelWidget, /*enabled=*/true, /*isOff=*/false,
                            "appearance.fuel_unit");
    }

    // Temperature unit toggle
    {
        ctx.addCycleControl("Temp unit", (UiConfig::getInstance().getTemperatureUnit() == TemperatureUnit::FAHRENHEIT) ? "F" : "C",
                            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE,
                            SettingsHud::ClickRegion::TEMP_UNIT_TOGGLE,
                            nullptr, /*enabled=*/true, /*isOff=*/false,
                            "appearance.temp_unit");
    }

    // Clock format toggle
    {
        ClockWidget* clockWidget = ctx.parent->getClockWidget();
        ctx.addCycleControl("Clock format", (clockWidget && clockWidget->getFormat24h()) ? "24h" : "12h",
                            SettingsHud::ClickRegion::CLOCK_FORMAT_TOGGLE,
                            SettingsHud::ClickRegion::CLOCK_FORMAT_TOGGLE,
                            clockWidget, /*enabled=*/true, /*isOff=*/false,
                            "appearance.clock_format");
    }

    // Compact time format toggle
    ctx.addToggleControl("Compact times", PluginData::getInstance().isShortTimeFormat(),
        SettingsHud::ClickRegion::SHORT_TIME_FORMAT_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.compact_times");

    // Drop shadow toggle
    ctx.addToggleControl("Drop shadow", UiConfig::getInstance().getDropShadow(),
        SettingsHud::ClickRegion::DROP_SHADOW_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.drop_shadow");

    // UI icons toggle (HUD title icons, settings tab/section icons, carets and pager
    // chevrons, settings + director buttons; off = text stand-ins)
    ctx.addToggleControl("UI icons", UiConfig::getInstance().getTitleIcons(),
        SettingsHud::ClickRegion::TITLE_ICONS_TOGGLE, nullptr, nullptr, 0, true,
        "appearance.hud_icons");

    // Motion: HUDs fade and slide in and out (core/motion.h). Off / Subtle / Normal.
    {
        const Motion::Level level = UiConfig::getInstance().getMotion();
        SettingsHud::CycleControl motion;
        motion.get = []() { return static_cast<int>(UiConfig::getInstance().getMotion()); };
        motion.set = [](int v) { UiConfig::getInstance().setMotion(static_cast<Motion::Level>(v)); };
        motion.count = Motion::LEVEL_COUNT;
        motion.nameOf = [](int v) { return std::string(Motion::levelName(static_cast<Motion::Level>(v))); };
        ctx.addCycleControl("Motion", Motion::levelName(level), motion, nullptr, true,
                            level == Motion::Level::OFF, "appearance.motion");
    }

    // Messages: the system toasts (core/system_messages.h) -- what a hotkey hid,
    // the profile auto-switch, a failed save. Separate from the achievement
    // toasts' own switch, which says nothing about these.
    {
        const bool on = SystemMessages::getInstance().isEnabled();
        static const char* const kOnOff[] = { "Off", "On" };
        SettingsHud::CycleControl messages;
        messages.count = 2;
        messages.get = []() { return SystemMessages::getInstance().isEnabled() ? 1 : 0; };
        messages.set = [](int v) { SystemMessages::getInstance().setEnabled(v != 0); };
        messages.nameOf = [](int v) { return std::string(kOnOff[v]); };
        ctx.addCycleControl("Messages", on ? "On" : "Off", messages, nullptr, true, !on,
                            "appearance.messages");
    }

    // UI scale: every HUD, this panel included, draws at its own Scale times
    // this, growing from its origin like its own Scale. A slider drag holds the
    // value back until release: applied live, the panel would grow or shrink
    // under the cursor and carry the track away from it.
    {
        static float s_dragPending = -1.0f;   // game thread only; < 0 = no drag
        const float shown = s_dragPending >= 0.0f ? s_dragPending : UiConfig::getInstance().getUiScale();
        char scaleValue[8];
        snprintf(scaleValue, sizeof(scaleValue), "%d%%", static_cast<int>(std::lround(shown * 100.0f)));
        SettingsHud::SteppedControl uiScale = SettingsHud::SteppedControl::accessor(
                [] { return s_dragPending >= 0.0f ? s_dragPending : UiConfig::getInstance().getUiScale(); },
                [](float v) { HudManager::getInstance().setUiScale(v); },
                0.05f, UiConfig::UI_SCALE_MIN, UiConfig::UI_SCALE_MAX, nullptr);
        uiScale.postStep = []() { HudManager::getInstance().requestPositionValidation(); };
        uiScale.dragSet = [](float v) { s_dragPending = v; };
        uiScale.onRelease = []() {
            if (s_dragPending < 0.0f) return;
            HudManager::getInstance().setUiScale(s_dragPending);
            s_dragPending = -1.0f;
        };
        ctx.addSteppedControl("UI scale", scaleValue, uiScale, nullptr, true, false, "appearance.ui_scale");
    }

    ctx.endColumns();

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
        // The value goes through the shared field like every other row's: the
        // shipped names (13-21 characters, "RobotoMono-Regular" renders as "Roboto
        // Mono Regular") fit it whole, and a user font with a longer name is cut at
        // the field rather than running through the ">" arrow beside it.
        //
        // NOTHING AFTER THE CONTROL. "Default" is simply one of the values the cycler
        // steps through, and the row reads as every other row on the tab. Trailing
        // the resolved name in muted parentheses -- "Default  (Roboto Mono Regular)"
        // -- would answer a question ("which font IS the default") that the row is
        // not asking, put a second column of text on six rows that no other control
        // has, and need its own truncation maths to stay inside the panel.
        ctx.addCycleControl(categoryName, isDefaultFont ? "Default" : fontDisplayName,
                            fontCycle(category), nullptr, true, false, tooltipId);
    };

    // TWO COLUMNS of three, label then the face's name: six full-width rows left
    // most of each row empty. The longest shipped name loses a character or two
    // at the field, as a user font with a long name already did.
    constexpr int FONT_LABEL_CHARS = 8;   // "Strong", "Digits" and the like
    ctx.beginColumns(2, 6, FONT_LABEL_CHARS);
    addFontRow(FontCategory::TITLE, "appearance.font_title");
    addFontRow(FontCategory::NORMAL, "appearance.font_normal");
    addFontRow(FontCategory::STRONG, "appearance.font_strong");
    addFontRow(FontCategory::DIGITS, "appearance.font_digits");
    addFontRow(FontCategory::MARKER, "appearance.font_marker");
    addFontRow(FontCategory::SMALL, "appearance.font_small");
    ctx.endColumns();

    // === COLORS SECTION ===
    ctx.addSectionHeading("Colors");

    // TWO COLUMNS of five: the base tones on the left (text greys and the panel
    // background), the signal colours on the right. Each slot is a short label, a
    // swatch and a cycler, which left most of a full-width row empty and made this
    // the tallest section on the tab.
    //
    // The label column is 12: the label (up to 10, "Background"), then the swatch
    // in the two characters before the "<".
    constexpr int COLOR_LABEL_CHARS = 12;
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);

    auto addColorCell = [&](ColorSlot slot, const char* tooltipId) {
        const unsigned long color = colorConfig.getColor(slot);

        // Colour swatch between the label and the cycler, narrower than a whole
        // character pair so the longest label still has a gap before it.
        {
            const float previewW = cw * 1.25f;
            const float previewH = ctx.lineHeightNormal * 0.55f;
            SPluginQuad_t previewQuad;
            float quadX = ctx.labelX + cw * 10.25f;
            float quadY = ctx.currentY + (ctx.lineHeightNormal - previewH) * 0.5f;
            ctx.parent->applyOffset(quadX, quadY);
            ctx.parent->setQuadPositions(previewQuad, quadX, quadY, previewW, previewH);
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
        ctx.addCycleControl(ColorConfig::getSlotName(slot),
                            isDefaultColor ? "Default" : ColorPalette::getColorName(color),
                            colorCycle(slot), nullptr, true, false, tooltipId);
    };

    ctx.beginColumns(2, 10, COLOR_LABEL_CHARS);
    addColorCell(ColorSlot::PRIMARY, "appearance.color_primary");
    addColorCell(ColorSlot::SECONDARY, "appearance.color_secondary");
    addColorCell(ColorSlot::TERTIARY, "appearance.color_tertiary");
    addColorCell(ColorSlot::MUTED, "appearance.color_muted");
    addColorCell(ColorSlot::BACKGROUND, "appearance.color_background");
    addColorCell(ColorSlot::ACCENT, "appearance.color_accent");
    addColorCell(ColorSlot::POSITIVE, "appearance.color_positive");
    addColorCell(ColorSlot::NEUTRAL, "appearance.color_neutral");
    addColorCell(ColorSlot::WARNING, "appearance.color_warning");
    addColorCell(ColorSlot::NEGATIVE, "appearance.color_negative");
    ctx.endColumns();

    // === COMPANION SECTION ===
    // The standalone window and its background, with the OBS recipe the key
    // colours exist for: a Window Capture is the only capture that can single the
    // window out (Game Capture hooks the game's own swapchain in the same
    // process), and a Chroma Key filter of the matching preset leaves the HUD.
    ctx.addSectionHeading("Companion window");
    ctx.beginColumns(2, 2, DISPLAY_LABEL_CHARS);
    // HUD display target: In-game / Companion (standalone window) / Both. A
    // < value > cycler with friendly labels; opens/closes the companion window on
    // change.
    {
        DisplayTarget target = UiConfig::getInstance().getDisplayTarget();
        const char* valueLabel = (target == DisplayTarget::COMPANION) ? "Companion"
                               : (target == DisplayTarget::BOTH)      ? "Both"
                                                                      : "In-game";
        static const char* const kTargets[] = { "In-game", "Companion", "Both" };
        SettingsHud::CycleControl targetCycle;
        targetCycle.count = 3;
        targetCycle.nameOf = [](int i) { return std::string(kTargets[i]); };
        targetCycle.get = []() { return static_cast<int>(UiConfig::getInstance().getDisplayTarget()); };
        // In-game suppression is applied live in HudManager::draw from the target;
        // the companion window opens or closes to match.
        targetCycle.set = [](int i) {
            const DisplayTarget next = static_cast<DisplayTarget>(i);
            UiConfig::getInstance().setDisplayTarget(next);
            CompanionWindow::getInstance().setEnabled(next != DisplayTarget::IN_GAME);
        };
        targetCycle.postStep = []() { HudManager::getInstance().markAllHudsDirty(); };
        targetCycle.repeat = false;   // a step opens or closes the companion window
        ctx.addCycleControl("HUD display", valueLabel, targetCycle,
                            /*targetHud=*/nullptr, /*enabled=*/true, /*isOff=*/false,
                            "appearance.display_target");
    }

    // Companion background: Dark, or a key colour matching OBS's Chroma Key presets
    // (the filter then shows just the HUD over the game). Greyed while the HUD
    // draws in-game only.
    {
        static const char* const kBackgrounds[] = { "Dark", "Green", "Blue", "Magenta" };
        static_assert(sizeof(kBackgrounds) / sizeof(kBackgrounds[0]) ==
                      static_cast<size_t>(CompanionBackground::COUNT),
                      "one label per CompanionBackground");
        const CompanionBackground bg = CompanionWindow::getInstance().getBackground();
        SettingsHud::CycleControl bgCycle;
        bgCycle.count = static_cast<int>(CompanionBackground::COUNT);
        bgCycle.nameOf = [](int i) { return std::string(kBackgrounds[i]); };
        bgCycle.get = []() { return static_cast<int>(CompanionWindow::getInstance().getBackground()); };
        bgCycle.set = [](int i) {
            CompanionWindow::getInstance().setBackground(static_cast<CompanionBackground>(i));
        };
        ctx.addCycleControl("Companion bg", kBackgrounds[static_cast<int>(bg)], bgCycle,
                            /*targetHud=*/nullptr,
                            UiConfig::getInstance().getDisplayTarget() != DisplayTarget::IN_GAME,
                            /*isOff=*/false, "appearance.companion_background");
    }
    ctx.endColumns();
    ctx.addNote("OBS: Window Capture of MXBMRP3 + Chroma Key of the bg colour");

    // No active HUD for appearance settings
    return nullptr;
}
