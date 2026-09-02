// ============================================================================
// hud/settings/settings_tab_about.cpp
// The About page: what this plugin is and where it came from.
//
// NOT IN THE TAB LIST. It is reached from the About button in the settings
// footer (ClickRegion::VERSION_CLICK), which is on screen from every tab. The
// sidebar is around thirty rows and is at or near what sets the panel's height,
// so a listed row costs layout everywhere; a page you read once does not need
// to cost one. See TabDescriptor::hidden.
//
// NO CONTROLS: everything here is prose or a link, so the page has no settings
// to reset and no click handler of its own. The support link reuses the General
// tab's addLinkRow + OPEN_LINK_KOFI region rather than growing a new one.
//
// TWO CONSTRAINTS ON THE COPY, both of which have already caught a draft:
//
//   * LINES DO NOT WRAP. Each is its own row, broken by hand at 49 of the 50 the
//     content column allows (settingsContentAreaChars - settingsLabelColumn,
//     one narrower with a themed card).
//   * ASCII ONLY. The in-game renderer is a byte-indexed 256-glyph CP1252
//     table, so an em-dash written as UTF-8 garbles on screen -- it is two
//     bytes, and each is drawn as its own glyph. Hyphens here; the README is
//     free to use whatever it likes.
//
// AND A BUDGET. This page is currently the TALLEST tab, so its length sets the
// height of the settings panel on every other tab too, and the panel does not
// scroll. settings_render_test holds the whole thing to one screen WITH
// developer mode on, which is the tighter of the two cases (it adds a row to
// General) and the one a 31-row draft of this text overflowed at 1.012 screens.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../version_widget.h"
#include "../../core/color_config.h"
#include "../../core/plugin_constants.h"

#include <cstdio>

BaseHud* SettingsHud::renderTabAbout(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("about");
    ColorConfig& colors = ColorConfig::getInstance();

    // The version rides the heading's HINT slot rather than a row of its own:
    // this page is the tallest tab and sets the panel height everywhere (see the
    // budget above), and the hint draws on the heading's OWN line, so it costs
    // nothing vertically. An About page is where people look for a version -- the
    // settings footer next to it is only a button, and the number is otherwise
    // on the Updates tab or the VersionWidget.
    char versionHint[32];
    snprintf(versionHint, sizeof(versionHint), "(v%s)", PluginConstants::PLUGIN_VERSION);
    ctx.addSectionHeading("About MXBMRP3", versionHint);
    ctx.addTextRow("MXBMRP3 is a free, open-source community project", colors.getSecondary());
    ctx.addTextRow("developed by one person in their spare time.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("It started in 2024 with MXBMRP - MX Bikes Memory", colors.getSecondary());
    ctx.addTextRow("Reader Project, a small experiment I built before", colors.getSecondary());
    ctx.addTextRow("I really knew what I was doing. That grew through", colors.getSecondary());
    ctx.addTextRow("several versions into MXBMRP3, a full plugin", colors.getSecondary());
    ctx.addTextRow("built on the game's plugin API.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("The project was created after development of", colors.getSecondary());
    ctx.addTextRow("MaxHUD, the community's long-standing HUD, came", colors.getSecondary());
    ctx.addTextRow("to an end. MXBMRP3 has since grown through", colors.getSecondary());
    ctx.addTextRow("community suggestions, testing, bug reports, and", colors.getSecondary());
    ctx.addTextRow("experimentation.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("There is no company or development team behind", colors.getSecondary());
    ctx.addTextRow("it. I develop and fund it in my spare time, but", colors.getSecondary());
    ctx.addTextRow("much of what it has become comes from the people", colors.getSecondary());
    ctx.addTextRow("who use it - the riders who report problems,", colors.getSecondary());
    ctx.addTextRow("suggest features, test new ideas, and keep", colors.getSecondary());
    ctx.addTextRow("finding new ways to use it.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("Keeping MXBMRP3 open source is deliberate. It", colors.getSecondary());
    ctx.addTextRow("means the project can be studied, contributed to,", colors.getSecondary());
    ctx.addTextRow("adapted, and built upon rather than disappearing", colors.getSecondary());
    ctx.addTextRow("with the person who made it.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("If MXBMRP3 makes the game a little better for", colors.getSecondary());
    ctx.addTextRow("you, or inspires something new, then it has done", colors.getSecondary());
    ctx.addTextRow("what I hoped it would.", colors.getSecondary());

    // The support link, moved here from the General tab: this is the page that
    // explains who funds the thing, so it is the one place a donation link reads
    // as context rather than as a solicitation. Docs and Discussion stay on
    // General, where someone looking for help will look.
    //
    // ITS OWN SECTION with the URL at labelX, rather than a "Support:" label with
    // the URL at column 18. That is the shape every other link row in the panel
    // uses and it is wrong here: those sit in a LIST, where the labels form a
    // column and the indent is what aligns the URLs with each other. A single row
    // has nothing to align with, so the indent reads as a gap with a stranded
    // label beside it. A heading says the same thing and leaves the URL flush with
    // the prose above it.
    ctx.addSectionHeading("Support");
    ctx.addLinkRow("", "https://ko-fi.com/thomas4f", 0,
                   SettingsHud::ClickRegion::OPEN_LINK_KOFI);

    return nullptr;   // no backing HUD
}
