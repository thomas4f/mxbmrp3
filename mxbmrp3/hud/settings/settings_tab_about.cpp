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
// to reset and no click handler of its own. The three thanks links reuse the
// General tab's addLinkRow and its link regions (OPEN_LINK_COMMUNITY is the
// same mxb-mods page the General tab's Discussion row opens); the regions are
// dispatched by SettingsHud::dispatchRegion's common cases, not by a tab
// handler -- with them in handleClickTabGeneral, this page's links did nothing.
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
    // Word for word the README's About section, so the two never drift: a
    // reader who has seen one recognises the other. Each fact once -- who makes
    // it, where it came from, why it is open source and free. ONE paragraph on
    // the last: this page does not scroll, and the themed panel overflowed the
    // screen when it was two (settings_render_test).
    ctx.addTextRow("MXBMRP3 is a community project: one person's", colors.getSecondary());
    ctx.addTextRow("spare-time work, and much of what it has become", colors.getSecondary());
    ctx.addTextRow("comes from the people who use it - the riders who", colors.getSecondary());
    ctx.addTextRow("report problems, suggest features, test new ideas,", colors.getSecondary());
    ctx.addTextRow("and keep finding new ways to use it.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("It started in 2024, after development of MaxHUD,", colors.getSecondary());
    ctx.addTextRow("the community's long-standing HUD, came to an", colors.getSecondary());
    ctx.addTextRow("end. The first version, MXBMRP - MX Bikes Memory", colors.getSecondary());
    ctx.addTextRow("Reader Project, was a small experiment I built", colors.getSecondary());
    ctx.addTextRow("before I really knew what I was doing. That grew", colors.getSecondary());
    ctx.addTextRow("through several versions into MXBMRP3, a full", colors.getSecondary());
    ctx.addTextRow("plugin built on the game's plugin API.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("Keeping MXBMRP3 open source is deliberate: it can", colors.getPrimary());
    ctx.addTextRow("be studied, changed and built on, and does not", colors.getPrimary());
    ctx.addTextRow("depend on me. It is free, with no paid tier, no", colors.getPrimary());
    ctx.addTextRow("locked features, no ads, and every release is", colors.getPrimary());
    ctx.addTextRow("built entirely from the public source (MIT).", colors.getPrimary());
    ctx.addSpacing();
    ctx.addTextRow("If MXBMRP3 makes the game a little better for", colors.getSecondary());
    ctx.addTextRow("you, or inspires something new, then it has done", colors.getSecondary());
    ctx.addTextRow("what I hoped it would.", colors.getSecondary());

    // The one place a way to say thanks is mentioned, and the only one: this is
    // the page that explains who makes the thing, so a link reads as context
    // rather than as a solicitation. Nothing else in the plugin asks -- the
    // post-update prompt that used to is gone. Docs and Discussion stay on
    // General, where someone looking for help will look.
    //
    // Three ways, in the order a player is most likely to take them, and the
    // README's sentence word for word: a comment or rating where the plugin is
    // downloaded, a star where it is built, and last the coffee. No reason is
    // given; the links say what they are.
    //
    // ITS OWN SECTION with the URLs at labelX, rather than "Support:" labels with
    // the URL at column 18. That is the shape every other link row in the panel
    // uses and it is wrong here: those sit in a LIST, where the labels form a
    // column and the indent is what aligns the URLs with each other. These rows
    // have the sentence above them to align with, so the indent would read as a
    // gap. A heading says the same thing and leaves the URLs flush with the prose.
    ctx.addSectionHeading("Say Thanks");
    ctx.addTextRow("A comment or rating on mxb-mods or a star on", colors.getSecondary());
    ctx.addTextRow("GitHub helps, and there is a Ko-fi. All optional.", colors.getSecondary());
    ctx.addLinkRow("", "https://mxb-mods.com/mxbmrp3", 0,
                   SettingsHud::ClickRegion::OPEN_LINK_COMMUNITY);
    ctx.addLinkRow("", "https://github.com/thomas4f/mxbmrp3", 0,
                   SettingsHud::ClickRegion::OPEN_LINK_GITHUB);
    ctx.addLinkRow("", "https://ko-fi.com/thomas4f", 0,
                   SettingsHud::ClickRegion::OPEN_LINK_KOFI);

    return nullptr;   // no backing HUD
}
