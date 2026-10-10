// ============================================================================
// hud/settings/settings_tab_widgets.cpp
// Tab renderer for Widgets settings (multi-widget table)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../game/game_config.h"
#include "../../core/hud_manager.h"
#include "../session_hud.h"
#include "../bars_widget.h"
#include "../clock_widget.h"
#include "../compass_widget.h"
#include "../crash_widget.h"
#include "../rpm_widget.h"
#include "../fuel_widget.h"
#include "../gamepad_widget.h"
#include "../gear_widget.h"
#include "../gforce_widget.h"
#include "../prestige_widget.h"
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

    // Column positions -- the same ones addWidgetRow draws its cells at.
    const SettingsLayoutContext::WidgetColumns cols = ctx.widgetColumns();

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
    const float headingY = ctx.addSectionHeading("Readouts");
    const auto columnLabel = [&](const char* text, float x) {
        ctx.parent->addString(text, x, headingY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::getStrong(),
            ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    };
    columnLabel("Visible", cols.visX);
    columnLabel("Title", cols.titleX);
    columnLabel("Texture", cols.texX);
    columnLabel("Opacity", cols.opacityX);
    columnLabel("Scale", cols.scaleX);

    // Each section lists its rows most-used first, the way the sidebar orders its
    // tabs (public usage report); Prestige stays last because it only appears once
    // earned, and Pointer, always on, is not measured.
    ctx.addWidgetRow("Speed", ctx.parent->getSpeedWidget(), true, true, true, true, "widgets.speed");
    ctx.addWidgetRow("Gear", ctx.parent->getGearWidget(), true, true, true, true, "widgets.gear");
    ctx.addWidgetRow("Lap", ctx.parent->getLapWidget(), true, true, true, true, "widgets.lap");
    ctx.addWidgetRow("Position", ctx.parent->getPositionWidget(), true, true, true, true, "widgets.position");
    ctx.addWidgetRow("Time", ctx.parent->getTimeWidget(), true, true, true, true, "widgets.time");
    // Note: SessionHud has its own dedicated tab with row configuration
    // THREE SECTIONS, not five: a heading and its card seam cost the panel more
    // than a row, and this tab is among the tallest. Crashes stays with the riding
    // readouts it shares one content box with (see crash_widget.cpp); Clock, the
    // real-world time, sits under Misc.
    ctx.addWidgetRow("Crashes", ctx.parent->getCrashWidget(), true, true, true, true, "widgets.crashes");
    ctx.addSectionHeading("Gauges");
    ctx.addWidgetRow("Fuel", ctx.parent->getFuelWidget(), true, true, true, true, "widgets.fuel");
    ctx.addWidgetRow("Bars", ctx.parent->getBarsWidget(), true, true, true, true, "widgets.bars");
    // The Title column is greyed on Speedo, Tacho, Gamepad, Pointer and
    // Settings, and that is the WIDGET's statement, not this list's: each sets
    // m_titleSupported = false because a TEXTURE is its panel, so a band would land
    // on the artwork rather than above it -- or, for the last three, because a cursor
    // and a button have nothing to caption.
    //
    // Tyre Temp and ECU are NOT in that list: they are plan panels with a content
    // card, exactly like Lean, G-Force and Compass in this section, and nothing about
    // their readout stops a band sitting above it. A bool per row could offer a
    // toggle the widget does not honour, which is invisible until someone tries it.
    ctx.addWidgetRow("Tacho", ctx.parent->getTachoWidget(), true, true, true, true, "widgets.tacho");
    // Fetched from HudManager like Prestige: nothing tab-specific to hold.
    ctx.addWidgetRow("RPM", &HudManager::getInstance().getRpmWidget(), true, true, true, true, "widgets.rpm");
    ctx.addWidgetRow("Lean", ctx.parent->getLeanWidget(), true, true, true, true, "widgets.lean");
    ctx.addWidgetRow("Speedo", ctx.parent->getSpeedoWidget(), true, true, true, true, "widgets.speedo");
    ctx.addWidgetRow("G-Force", ctx.parent->getGForceWidget(), true, true, true, true, "widgets.gforce");
    // The compass is the one gauge that BUILDS a title (through the caption path, so it
    // gets the themed band and reserves a row for it). A row disagreeing with the
    // widget would make it the only captionable panel a user cannot caption -- the
    // disagreement m_titleSupported makes impossible. Speedo and Tacho draw no title
    // and say so themselves.
    ctx.addWidgetRow("Compass", ctx.parent->getCompassWidget(), true, true, true, true, "widgets.compass");
#if GAME_HAS_TYRE_TEMP
    ctx.addWidgetRow("Tyre Temp", ctx.parent->getTyreTempWidget(), true, true, true, true, "widgets.tyre_temp");
#endif
#if GAME_HAS_ECU
    ctx.addWidgetRow("ECU", ctx.parent->getEcuWidget(), true, true, true, true, "widgets.ecu");
#endif
    ctx.addSectionHeading("Misc");
    ctx.addWidgetRow("Settings", ctx.parent->getSettingsButtonWidget(), true, true, true, true, "widgets.settings_button");
    ctx.addWidgetRow("Version", ctx.parent->getVersionWidget(), true, true, true, true, "widgets.version");
    // The Gamepad's Texture column picks the PAD (gamepads/<name>/) rather than a
    // texture variant; addWidgetRow routes on BaseHud::m_packKind, so there is no
    // flag to pass here and no way for this call site to disagree with it.
    ctx.addWidgetRow("Gamepad", ctx.parent->getGamepadWidget(), true, true, true, true, "widgets.gamepad");
    ctx.addWidgetRow("Clock", ctx.parent->getClockWidget(), true, true, true, true, "widgets.clock");
    // Pointer: the visibility toggle drives the menu-only-cursor mode (On = shown while
    // racing, Off = only in the settings menu). It can't toggle the widget's real
    // visibility because the pointer must stay drawable to appear in the menu.
    ctx.addWidgetRow("Pointer", ctx.parent->getPointerWidget(), false, true, false, true, "widgets.pointer", /*menuOnlyPointerRow=*/true);
    // Prestige is the one row in this table that is EARNED. Left out entirely
    // rather than greyed: a greyed row is a control you have not found yet, and
    // this one you have not got. Fetched from HudManager rather than through a
    // SettingsHud member, like the achievement widget on its own tab -- there is
    // nothing tab-specific to hold. Its Texture column is the ordinary variant
    // cycle: the badge artwork IS the widget (prestige_widget.h).
    if (PrestigeWidget* badge = HudManager::getInstance().getPrestigeWidget()) {
        if (PrestigeWidget::isUnlocked()) {
            ctx.addWidgetRow("Prestige", badge, true, true, true, true, "widgets.prestige");
        }
    }

    // No active HUD for multi-widget tab
    return nullptr;
}
