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
    // Parameters: name, hud, enableVisibility, enableTitle, enableBgTexture, enableOpacity, enableScale, tooltipId
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Timing");
    ctx.addWidgetRow("Position", ctx.parent->getPositionWidget(), true, true, true, true, true, "widgets.position");
    ctx.addWidgetRow("Lap", ctx.parent->getLapWidget(), true, true, true, true, true, "widgets.lap");
    ctx.addWidgetRow("Time", ctx.parent->getTimeWidget(), true, true, true, true, true, "widgets.time");
    ctx.addWidgetRow("Clock", ctx.parent->getClockWidget(), true, true, true, true, true, "widgets.clock");
    // Note: SessionHud now has its own dedicated tab with row configuration
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Speed & Gear");
    ctx.addWidgetRow("Gear", ctx.parent->getGearWidget(), true, true, true, true, true, "widgets.gear");
    ctx.addWidgetRow("Speed", ctx.parent->getSpeedWidget(), true, true, true, true, true, "widgets.speed");
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Telemetry");
    ctx.addWidgetRow("Bars", ctx.parent->getBarsWidget(), true, true, true, true, true, "widgets.bars");
    ctx.addWidgetRow("Lean", ctx.parent->getLeanWidget(), true, true, true, true, true, "widgets.lean");
    ctx.addWidgetRow("G-Force", ctx.parent->getGForceWidget(), true, true, true, true, true, "widgets.gforce");
    ctx.addWidgetRow("Fuel", ctx.parent->getFuelWidget(), true, true, true, true, true, "widgets.fuel");
#if GAME_HAS_TYRE_TEMP
    ctx.addWidgetRow("Tyre Temp", ctx.parent->getTyreTempWidget(), true, false, true, true, true, "widgets.tyre_temp");
#endif
#if GAME_HAS_ECU
    ctx.addWidgetRow("ECU", ctx.parent->getEcuWidget(), true, false, true, true, true, "widgets.ecu");
#endif
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Gauges");
    ctx.addWidgetRow("Speedo", ctx.parent->getSpeedoWidget(), true, false, true, true, true, "widgets.speedo");
    ctx.addWidgetRow("Tacho", ctx.parent->getTachoWidget(), true, false, true, true, true, "widgets.tacho");
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Misc");
    ctx.addWidgetRow("Gamepad", ctx.parent->getGamepadWidget(), true, false, true, true, true, "widgets.gamepad");
    ctx.addWidgetRow("Pointer", ctx.parent->getPointerWidget(), false, false, true, false, true, "widgets.pointer");
    ctx.addWidgetRow("Settings", ctx.parent->getSettingsButtonWidget(), true, false, true, true, true, "widgets.settings_button");
    ctx.addWidgetRow("Version", ctx.parent->getVersionWidget(), true, false, true, true, true, "widgets.version");

    // Info text
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("More options available in mxbmrp3_settings.ini", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        ColorConfig::getInstance().getMuted(), ctx.fontSize * 0.9f);

    // No active HUD for multi-widget tab
    return nullptr;
}
