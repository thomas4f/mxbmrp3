// ============================================================================
// hud/settings/settings_tab_widgets.cpp
// Tab renderer for Widgets settings (multi-widget table)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../game/game_config.h"
#include "../session_hud.h"
#include "../bars_widget.h"
#include "../clock_widget.h"
#include "../compass_widget.h"
#include "../crash_widget.h"
#include "../fuel_widget.h"
#include "../gamepad_widget.h"
#include "../gear_widget.h"
#include "../gforce_widget.h"
#include "../lap_widget.h"
#include "../lean_widget.h"
#include "../pointer_widget.h"
#include "../position_widget.h"
#include "../settings_button_widget.h"
#include "../speed_widget.h"
#include "../speedo_widget.h"
#include "../tacho_widget.h"
#include "../time_widget.h"
#include "../version_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../ecu_widget.h"
#endif

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabWidgets(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("widgets");

    // Column positions -- must match addWidgetRow's exactly.
    float nameX = ctx.labelX;
    float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);
    float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, ctx.fontSize);
    float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, ctx.fontSize);

    // The column labels ride on the FIRST section's heading row, in the columns they
    // caption -- the same shape settings_tab_hotkeys.cpp uses, and for the same
    // reason. A bare row drawn before any section is opened would sit in the gap
    // between the description block and the first card: outside both, on the
    // description card's bottom edge. A label with no surface behind it reads as a
    // rendering fault.
    //
    // No "Widget" label: the section name ("Timing", "Gauges") stands in that column
    // and captions it better than the word would.
    // Parameters: name, hud, enableVisibility, enableBgTexture, enableOpacity,
    // enableScale, tooltipId. There is no enableTitle -- the Title column reads
    // BaseHud::m_titleSupported, so this list cannot disagree with the widgets.
    const float headingY = ctx.addSectionHeading("Timing");
    const auto columnLabel = [&](const char* text, float x) {
        ctx.parent->addString(text, x, headingY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::getStrong(),
            ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    };
    columnLabel("Visible", visX);
    columnLabel("Title", titleX);
    columnLabel("Texture", bgTexX);
    columnLabel("Opacity", opacityX);
    columnLabel("Scale", scaleX);

    ctx.addWidgetRow("Position", ctx.parent->getPositionWidget(), true, true, true, true, "widgets.position");
    ctx.addWidgetRow("Lap", ctx.parent->getLapWidget(), true, true, true, true, "widgets.lap");
    ctx.addWidgetRow("Time", ctx.parent->getTimeWidget(), true, true, true, true, "widgets.time");
    ctx.addWidgetRow("Clock", ctx.parent->getClockWidget(), true, true, true, true, "widgets.clock");
    // Note: SessionHud has its own dedicated tab with row configuration
    // "Big Readouts" rather than "Speed & Gear": the three below share one content
    // box on purpose (see crash_widget.cpp), so they are grouped by the shape they
    // draw, not by what they are about. Crashes is here for that reason alone -- it
    // has nothing to do with the bike, and everything to do with tiling beside them.
    ctx.addSectionHeading("Big Readouts");
    ctx.addWidgetRow("Gear", ctx.parent->getGearWidget(), true, true, true, true, "widgets.gear");
    ctx.addWidgetRow("Speed", ctx.parent->getSpeedWidget(), true, true, true, true, "widgets.speed");
    ctx.addWidgetRow("Crashes", ctx.parent->getCrashWidget(), true, true, true, true, "widgets.crashes");
    ctx.addSectionHeading("Telemetry");
    ctx.addWidgetRow("Bars", ctx.parent->getBarsWidget(), true, true, true, true, "widgets.bars");
    ctx.addWidgetRow("Lean", ctx.parent->getLeanWidget(), true, true, true, true, "widgets.lean");
    ctx.addWidgetRow("G-Force", ctx.parent->getGForceWidget(), true, true, true, true, "widgets.gforce");
    ctx.addWidgetRow("Fuel", ctx.parent->getFuelWidget(), true, true, true, true, "widgets.fuel");
#if GAME_HAS_TYRE_TEMP
    ctx.addWidgetRow("Tyre Temp", ctx.parent->getTyreTempWidget(), true, true, true, true, "widgets.tyre_temp");
#endif
#if GAME_HAS_ECU
    ctx.addWidgetRow("ECU", ctx.parent->getEcuWidget(), true, true, true, true, "widgets.ecu");
#endif
    // The Title column is greyed from here down (Speedo, Tacho, Gamepad, Pointer,
    // Settings), and that is the WIDGET's statement, not this list's: each sets
    // m_titleSupported = false because a TEXTURE is its panel, so a band would land
    // on the artwork rather than above it -- or, for the last three, because a cursor
    // and a button have nothing to caption.
    //
    // Tyre Temp and ECU are NOT in that list: they are plan panels with a content
    // card, exactly like Lean, G-Force and Compass beside them, and nothing about
    // their readout stops a band sitting above it. A bool per row could offer a
    // toggle the widget does not honour, which is invisible until someone tries it.
    ctx.addSectionHeading("Gauges");
    ctx.addWidgetRow("Speedo", ctx.parent->getSpeedoWidget(), true, true, true, true, "widgets.speedo");
    ctx.addWidgetRow("Tacho", ctx.parent->getTachoWidget(), true, true, true, true, "widgets.tacho");
    // The compass is the one gauge that BUILDS a title (through the caption path, so it
    // gets the themed band and reserves a row for it). A row disagreeing with the
    // widget would make it the only captionable panel a user cannot caption -- the
    // disagreement m_titleSupported makes impossible. Speedo and Tacho draw no title
    // and say so themselves.
    ctx.addWidgetRow("Compass", ctx.parent->getCompassWidget(), true, true, true, true, "widgets.compass");
    ctx.addSectionHeading("Misc");
    // The Gamepad's Texture column picks the PAD (gamepads/<name>/) rather than a
    // texture variant; addWidgetRow routes on BaseHud::m_packKind, so there is no
    // flag to pass here and no way for this call site to disagree with it.
    ctx.addWidgetRow("Gamepad", ctx.parent->getGamepadWidget(), true, true, true, true, "widgets.gamepad");
    // Pointer: the visibility toggle drives the menu-only-cursor mode (On = shown while
    // racing, Off = only in the settings menu). It can't toggle the widget's real
    // visibility because the pointer must stay drawable to appear in the menu.
    ctx.addWidgetRow("Pointer", ctx.parent->getPointerWidget(), false, true, false, true, "widgets.pointer", /*menuOnlyPointerRow=*/true);
    ctx.addWidgetRow("Settings", ctx.parent->getSettingsButtonWidget(), true, true, true, true, "widgets.settings_button");
    ctx.addWidgetRow("Version", ctx.parent->getVersionWidget(), true, true, true, true, "widgets.version");

    ctx.addNote("Tip: more options are available in mxbmrp3_settings.ini");

    // No active HUD for multi-widget tab
    return nullptr;
}
