// ============================================================================
// hud/settings/settings_tab_widgets.cpp
// Tab renderer for Widgets settings (multi-widget table)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../game/game_config.h"

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabWidgets(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("widgets");

    // Table header - columns must match addWidgetRow positions exactly
    float nameX = ctx.labelX;
    float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);
    float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, ctx.fontSize);

    ctx.parent->addString("Widget", nameX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getStrong(),
        ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    ctx.parent->addString("Visible", visX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getStrong(),
        ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    ctx.parent->addString("Title", titleX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getStrong(),
        ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    ctx.parent->addString("Texture", bgTexX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getStrong(),
        ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    ctx.parent->addString("Opacity", opacityX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getStrong(),
        ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    ctx.parent->addString("Scale", scaleX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getStrong(),
        ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    ctx.currentY += ctx.lineHeightNormal;

    // Widget rows
    // Parameters: name, hud, enableTitle, enableOpacity, enableScale, enableVisibility, enableBgTexture, tooltipId
    ctx.addWidgetRow("Lap", ctx.parent->getLapWidget(), true, true, true, true, true, "widgets.lap");
    ctx.addWidgetRow("Position", ctx.parent->getPositionWidget(), true, true, true, true, true, "widgets.position");
    ctx.addWidgetRow("Time", ctx.parent->getTimeWidget(), true, true, true, true, true, "widgets.time");
    // Note: SessionHud now has its own dedicated tab with row configuration
    ctx.addWidgetRow("Speed", ctx.parent->getSpeedWidget(), false, true, true, true, true, "widgets.speed");
    ctx.addWidgetRow("Speedo", ctx.parent->getSpeedoWidget(), false, true, true, true, true, "widgets.speedo");
    ctx.addWidgetRow("Tacho", ctx.parent->getTachoWidget(), false, true, true, true, true, "widgets.tacho");
    ctx.addWidgetRow("Bars", ctx.parent->getBarsWidget(), false, true, true, true, true, "widgets.bars");
    ctx.addWidgetRow("Notices", ctx.parent->getNoticesWidget(), false, true, true, true, true, "widgets.notices");
    ctx.addWidgetRow("Fuel", ctx.parent->getFuelWidget(), true, true, true, true, true, "widgets.fuel");
    ctx.addWidgetRow("Gamepad", ctx.parent->getGamepadWidget(), false, true, true, true, true, "widgets.gamepad");
    ctx.addWidgetRow("Lean", ctx.parent->getLeanWidget(), false, true, true, true, true, "widgets.lean");
#if GAME_HAS_TYRE_TEMP
    ctx.addWidgetRow("Tyre Temp", ctx.parent->getTyreTempWidget(), false, true, true, true, true, "widgets.tyre_temp");
#endif
    ctx.addWidgetRow("Pointer", ctx.parent->getPointerWidget(), false, false, true, false, true, "widgets.pointer");
    ctx.addWidgetRow("Version", ctx.parent->getVersionWidget(), false, false, false, true, false, "widgets.version");

    // Info text
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("More options available in mxbmrp3_settings.ini", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        ColorConfig::getInstance().getMuted(), ctx.fontSize * 0.9f);

    // No active HUD for multi-widget tab
    return nullptr;
}
