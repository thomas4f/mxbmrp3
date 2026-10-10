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
// to reset and no click handler of its own. The links use the shared link
// regions (OPEN_LINK_COMMUNITY is the mxb-mods discussion page); the regions are
// dispatched by SettingsHud::dispatchRegion's common cases, not by a tab
// handler -- with them in handleClickTabGeneral, this page's links did nothing.
//
// TWO CONSTRAINTS ON THE COPY, both of which have already caught a draft:
//
//   * LINES DO NOT WRAP. Each is its own row, broken by hand within the 58 the
//     content column allows (settingsContentAreaChars - settingsLabelColumn,
//     one narrower with a themed card), and worded so no paragraph ends on a
//     lone word.
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
#include "../../core/font_config.h"
#include "../../core/plugin_constants.h"
#include "../../core/plugin_utils.h"

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
    ctx.addTextRow("MXBMRP3 is a community project: one person's spare-time", colors.getSecondary());
    ctx.addTextRow("work, and much of what it has become comes from the people", colors.getSecondary());
    ctx.addTextRow("who use it - the riders who report problems, suggest", colors.getSecondary());
    ctx.addTextRow("features, test new ideas and find new ways to use it.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("It started in 2024, after development of MaxHUD, the", colors.getSecondary());
    ctx.addTextRow("community's long-standing HUD, came to an end. The first", colors.getSecondary());
    ctx.addTextRow("version, MXBMRP - MX Bikes Memory Reader Project, was a", colors.getSecondary());
    ctx.addTextRow("small experiment I built before I really knew what I was", colors.getSecondary());
    ctx.addTextRow("doing. That grew through several versions into MXBMRP3, a", colors.getSecondary());
    ctx.addTextRow("full plugin built on the game's plugin API.", colors.getSecondary());
    ctx.addSpacing();
    ctx.addTextRow("Keeping MXBMRP3 open source is deliberate: it can be", colors.getPrimary());
    ctx.addTextRow("studied, changed and built on, and does not depend on me.", colors.getPrimary());
    ctx.addTextRow("It is free, with no paid tier, no locked features and no", colors.getPrimary());
    ctx.addTextRow("ads, and every release is built from the public source.", colors.getPrimary());
    ctx.addSpacing();
    ctx.addTextRow("If MXBMRP3 makes the game a little better for you, or", colors.getSecondary());
    ctx.addTextRow("inspires something new, it has done what I hoped it would.", colors.getSecondary());

    // The one place a way to say thanks is mentioned, and the only one: this is
    // the page that explains who makes the thing, so a link reads as context
    // rather than as a solicitation. Nothing else in the plugin asks -- the
    // post-update prompt that used to is gone.
    //
    // Three ways, in the order a player is most likely to take them, and the
    // README's sentence less its links: a comment or rating where the plugin is
    // downloaded, a star where it is built, and last the coffee. No reason is
    // given; the links say what they are.
    //
    // ITS OWN SECTION with the links at labelX, rather than "Support:" labels with
    // the URL at column 18. That is the shape every other link row in the panel
    // uses and it is wrong here: those sit in a LIST, where the labels form a
    // column and the indent is what aligns the URLs with each other. These rows
    // have the sentence above them to align with, so the indent would read as a
    // gap. A heading says the same thing and leaves the URLs flush with the prose.
    // "Thanks" and "Help" SIDE BY SIDE in the one card, a faint rule between
    // them: two sections, without a second card's seam, which would make this
    // page -- among the tallest -- set the panel height. Same four rows as one
    // section: a heading, then three lines.
    //
    // The README's sentence, then its three names as links on a row of their
    // own, each at a FIXED COLUMN. Not in place inside the sentence: a link's x
    // there is the width of the text before it, counted in monospace cells, and
    // a proportional font draws that text narrower or wider, so the link landed
    // in a gap or on top of a word.
    const float headingY = ctx.addSectionHeading("Thanks");
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    constexpr int THANKS_CHARS = 36;   // the sentence's longest line
    constexpr int GAP_CHARS = 3;
    const float helpX = ctx.labelX + cw * static_cast<float>(THANKS_CHARS + GAP_CHARS);
    ctx.parent->addString("Help", helpX, headingY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    {
        auto text = [&](const char* t, float x) {
            ctx.parent->addString(t, x, ctx.currentY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
        };
        text("A comment or rating, a star or a", ctx.labelX);
        text("Setup, every setting", helpX);
        ctx.nextLine();
        text("coffee helps. All optional.", ctx.labelX);
        text("and the web overlay.", helpX);
        ctx.nextLine();
        // Columns with room to spare past each name, so no font runs one into the next.
        ctx.addLinkCell(ctx.labelX, "mxb-mods", SettingsHud::ClickRegion::OPEN_LINK_COMMUNITY);
        ctx.addLinkCell(ctx.labelX + cw * 12.0f, "GitHub", SettingsHud::ClickRegion::OPEN_LINK_GITHUB);
        ctx.addLinkCell(ctx.labelX + cw * 22.0f, "Ko-fi", SettingsHud::ClickRegion::OPEN_LINK_KOFI);
        ctx.addLinkCell(helpX, "Docs & guides", SettingsHud::ClickRegion::OPEN_LINK_DOCS);
        ctx.nextLine();
        // The rule, centred in the gap, from the headings down to the last line.
        const float ruleX = ctx.labelX + cw * (static_cast<float>(THANKS_CHARS) + GAP_CHARS * 0.5f);
        ctx.addSolidQuad(ruleX, headingY + ctx.lineHeightNormal * 0.15f, cw * 0.08f,
                         ctx.currentY - headingY - ctx.lineHeightNormal * 0.3f,
                         PluginUtils::applyOpacity(colors.getMuted(), 0.5f));
    }

    return nullptr;   // no backing HUD
}
