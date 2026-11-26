// ============================================================================
// hud/settings/settings_tab_achievements.cpp
// Tab renderer for Achievements: the lifetime numbers the plugin keeps, shown
// as tiered progress toward the next Bronze/Silver/Gold/Platinum threshold --
// one entry is both the number and the achievement it feeds. Plus the toast
// card's controls (global, like the Director's settings). The on-track
// Stats HUD is not here: it keeps its own per-profile tab (settings_tab_stats).
//
// Global manager tab, like Spotter/Director: drives AchievementManager
// directly and returns nullptr (no backing HUD).
//
// TWO ROWS PER ENTRY, PAGED, ONE ANCHOR EACH. The marker icon spans BOTH rows
// (the Steam card's shape): it is what makes two lines read as one entry, and
// the first cut drew it on the title row alone, which left every entry looking
// like two unrelated lines. Row 1 is identity -- title, and the tier earned at
// the right edge; row 2 is the task -- what the NEXT tier asks in words
// ("Finish 10 races", the toast's own sentence), and the numbers at the right
// edge. The progress is the BAND behind both rows, filled from the left: the
// same tint the hover band uses, and the entry is no hover region at all --
// one band per entry, not two. The summary block
// above the list takes the same shape, so the eye learns it once.
// Every tab shares one panel height set by the tallest (settings_fit_test /
// theme_geometry_test); ENTRIES_PER_PAGE keeps this tab under that ceiling.
// EACH GROUP IS A CARD (Achievements::Group, the card's heading), all in one
// scrolling list: related rows read together. A group with no row on this
// game (a kart has no Freestyle) is no card, and a hidden row is not a row
// until it is earned (Achievements::Entry::hidden). The row says what the
// next tier asks; it has no hover of its own (no tooltip, no tint) --
// nothing to find by moving the mouse.
//
// ORDER within a group: the catalogue's, always. Rows used to re-sort on every
// rebuild (earned first, then the closest locked), so the row being watched
// jumped the moment it moved; a fixed order is one you can learn.
// No unlock dates: none are collected (see AchievementManager::State).
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../achievement_widget.h"
#include "../../core/achievement_manager.h"
#include "../../core/achievement_text.h"
#include "../../core/achievements.h"
#include "../../core/completion_floor.h"
#include "../../core/asset_manager.h"
#include "../../core/color_config.h"
#include "../../core/hud_manager.h"
#include "../../core/plugin_constants.h"
#include "../../core/plugin_utils.h"
#include "../../core/stats_manager.h"
#include "../../core/ui_config.h"
#include "../../game/game_config.h"
#if GAME_HAS_ANALYTICS
#include "../../core/analytics_manager.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace PluginConstants;

namespace {

using Achievements::ENTRIES_PER_PAGE;   // the list's height in rows

}  // namespace

bool SettingsHud::handleClickTabAchievements(const ClickRegion& region) {
    // ACHIEVEMENTS_TOASTS_TOGGLE is deliberately NOT here: the tab list carries
    // that same toggle as its row checkbox, and a tab-scoped handler only runs
    // while its own tab is open -- so it lives in dispatchRegion's common switch
    // (settings_hud_input.cpp), like the spotter's.
    switch (region.type) {
        // ARM, then PERFORM -- the Reset buttons' two-step (settings_tab_general),
        // and for a stronger reason: this one is not undoable by anything, not
        // even a reset. StatsManager::prestige() re-checks the gate, so an armed
        // button that somehow outlived the Platinum Sweep still does nothing.
        case ClickRegion::ACHIEVEMENTS_PRESTIGE:
            if (m_prestigeConfirmed) {
                m_prestigeConfirmed = false;
                // The re-check can refuse (see prestige()), and a refusal is not
                // a trade: the button still disarms and the panel still redraws
                // for it, but nothing is reported. An event that counted clicks
                // rather than prestiges would put the rarest act in the plugin
                // behind a number that is not it.
                const bool taken = StatsManager::getInstance().prestige();
#if GAME_HAS_ANALYTICS
                // The rarest act in the plugin, and the only one that throws a
                // finished ladder away: worth knowing whether anybody does it,
                // and how far they go. The LEVEL is the dimension, read after
                // the trade so it is the one just reached.
                if (taken) {
                    AnalyticsManager::getInstance().trackEvent(
                        "prestige_taken",
                        {{"level", std::to_string(StatsManager::getInstance().getPrestige())}});
                }
#else
                (void)taken;
#endif
                rebuildRenderData();
            } else {
                m_prestigeConfirmed = true;
                m_resetProfileConfirmed = false;
                m_resetAllConfirmed = false;
                rebuildRenderData();
            }
            return true;
        default:
            return false;
    }
}

BaseHud* SettingsHud::renderTabAchievements(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("achievements");

    AchievementManager& ach = AchievementManager::getInstance();
    AchievementWidget* widget = HudManager::getInstance().getAchievementWidget();
    ColorConfig& colors = ColorConfig::getInstance();
    const bool toastsOn = ach.isToastsEnabled();
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    const float rowRight = ctx.labelX + ctx.rowSpanWidth();

    char buf[64];

    // === TOASTS: the card's own rows ===
    // Built from the standard row helpers, at the control column with the standard
    // value width, so the arrows line up with every other tab's Appearance rows.
    ctx.addSectionHeading(TR("Toasts"));

    // Every row but Visible greys out with the toasts off: live arrows under an
    // Off switch read as a control that does nothing.

    // Two columns (beginColumns), like every HUD tab's Appearance block.
    ctx.beginColumns(2, widget ? 6 : 2);

    // Visible: the toast master (common handler, shared with the tab-list checkbox).
    ctx.addToggleControl(TR("Visible"), toastsOn,
        SettingsHud::ClickRegion::ACHIEVEMENTS_TOASTS_TOGGLE, nullptr, nullptr, 0, true,
        "achievements.toasts");

    if (widget) {
        ctx.addToggleControl(TR("Title"), widget->getShowTitle(),
            SettingsHud::ClickRegion::TITLE_TOGGLE, widget, nullptr, 0, toastsOn, "common.title");

        // Texture or Theme: the rule addStandardHudControls applies -- a HUD with
        // texture variants cycles them, everything else cycles its panel theme
        // under the "Theme" label and tooltip.
        const bool hasTextures = !widget->getAvailableTextureVariants().empty();
        if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
            const std::string& ov = widget->getThemeOverride();
            std::string themeValue = TR("Default");
            if (ov == BaseHud::THEME_NONE) {
                themeValue = TR("None");
            } else if (!ov.empty()) {
                if (const ThemeAsset* t = AssetManager::getInstance().getThemeByName(ov)) {
                    themeValue = t->displayName;
                }
            }
            ctx.addCycleControl(TR("Theme"), themeValue.c_str(), SettingsHud::themeOverrideCycle(widget),
                widget, toastsOn, false, "common.theme");
        } else {
            char texValue[8];
            const int variant = widget->getTextureVariant();
            snprintf(texValue, sizeof(texValue), (!hasTextures || variant == 0) ? TR("Off") : "%d", variant);
            ctx.addCycleControl(TR("Texture"), texValue, SettingsHud::textureCycle(widget),
                widget, toastsOn && hasTextures, !hasTextures || variant == 0, "common.texture");
        }

        ctx.addOpacityControl(widget, toastsOn);
        ctx.addScaleControl(widget, toastsOn);
    }

    // Duration: the manager's own value, a data-driven stepped control.
    snprintf(buf, sizeof(buf), "%ds", ach.getToastDurationMs() / 1000);
    ctx.addSteppedControl(TR("Duration"), buf,
        SettingsHud::SteppedControl::clampInt(
            ach.toastDurationMsPtr(), AchievementManager::TOAST_DURATION_STEP_MS,
            AchievementManager::MIN_TOAST_DURATION_MS, AchievementManager::MAX_TOAST_DURATION_MS,
            widget),
        nullptr, toastsOn, false, "achievements.toast_duration", /*tooltipOnArrows=*/false);
    ctx.endColumns();

    // === ENTRY GEOMETRY, shared by the summary block and the list ===
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const AssetManager& assets = AssetManager::getInstance();
    // The tile behind a marker: the badge textures (textures/badge_N.tga, one
    // per tier and a step fancier each -- greyscale, so the metal tints them),
    // or a flat quad when a tier's texture is missing. Looked up once per
    // rebuild, not per row. A locked row takes the plainest; an earned one-shot
    // the richest, since "earned" is its top.
    int badgeSprite[Achievements::TIER_COUNT + 1] = {};
    if (useIcons) {
        for (int t = 1; t <= Achievements::TIER_COUNT; ++t) badgeSprite[t] = assets.getSpriteIndex("badge", t);
    }
    // The band's breathing room: a sliver off each end so two entries' bands
    // read as two, not one tall one.
    const float bandGap = ctx.lineHeightNormal * 0.1f;
    const float entryH = ctx.lineHeightNormal * 2.0f - 2.0f * bandGap;
    // The icon sits on a TILE in the tier's metal, square on screen and as tall
    // as the entry, and the band starts to its right: the tile says the tier at
    // a glance, the band says how far to the next. Without icons the tile is
    // gone and the band starts at labelX like every other row.
    const float tileW = useIcons ? entryH / UI_ASPECT_RATIO : 0.0f;
    const float bandX = ctx.labelX + tileW + (useIcons ? cw * 0.5f : 0.0f);
    const float textX = bandX + cw * 0.5f;

    // One two-row entry on a band: [icon] title ..... tag / task ..... numbers.
    // `tagStrong` draws the tag in the STRONG face (an earned tier); a locked
    // row's "Locked" and the summary's percentage stay in the normal face, so
    // only metal is bold. The task is cut to the room the numbers leave (a net:
    // the unit gate holds every shipped row under Achievements::TAB_ROW_CHARS,
    // so only a count grown far past Platinum ever reaches it).
    // WHO is carrying a per-track or per-bike maximum row is part of the TITLE
    // STRING -- "Local Hero (Southwick)" -- not a second string positioned after
    // it. Two strings meant measuring the title in character cells to place the
    // name, and the title is drawn in the STRONG face while the name was drawn in
    // the normal one: two faces with different advance widths, so the gap between
    // them was right in one font and wrong in every other. One string cannot
    // drift from itself.
    auto addEntry = [&](int sprite, int badge, unsigned long tileColor, const char* title,
                        const char* tag, unsigned long tagColor, bool tagStrong,
                        float fraction, unsigned long fillColor,
                        const char* task, const char* numbers) {
        const float top = ctx.currentY + bandGap;
        ctx.addProgressBand(bandX, top, rowRight - bandX, entryH, fraction, fillColor);
        if (sprite > 0) {
            // The glyph in the tile's own hue, lifted off it in luma the way the gap
            // bar's and the notices' captions are lifted off their slabs
            // (legibleOnFill): tone on tone, never the same tone.
            //
            // NO TILE UNDER AN UNEARNED ROW (badge 0). It used to take a flat muted
            // square where the metal art goes, so a fresh page was a column of grey
            // boxes with the earned rows competing against them. The BAND stays --
            // that is what marks the entry's extent, and it is the same band on
            // every row - but the tile arrives with the tier it is there to show.
            // With nothing under it the glyph has nothing to lift off, so it is
            // drawn in the hue itself: muted, which is a locked row's colour.
            const float tileCx = ctx.labelX + tileW * 0.5f;
            const float tileCy = ctx.currentY + ctx.lineHeightNormal;
            if (badge > 0) ctx.parent->addIcon(tileCx, tileCy, badge, tileColor, entryH);
            ctx.parent->addIcon(tileCx, tileCy, sprite,
                                badge > 0 ? ctx.parent->legibleOnFill(tileColor, tileColor) : tileColor,
                                entryH * 0.72f);
        }
        // Cut to whatever the tag leaves, like the task line below: a title that
        // has picked up a long track name becomes "Local Hero (Southw..." rather
        // than running under the tier tag.
        const int titleRoom = static_cast<int>((rowRight - textX) / cw)
                            - (tag[0] ? static_cast<int>(std::strlen(tag)) + 1 : 0);
        char titleCut[96];
        if (static_cast<int>(std::strlen(title)) > titleRoom && titleRoom > 3) {
            snprintf(titleCut, sizeof(titleCut), "%.*s...", titleRoom - 3, title);
            title = titleCut;
        }
        ctx.parent->addString(title, textX, ctx.currentY, Justify::LEFT,
            Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
        ctx.parent->addString(tag, rowRight, ctx.currentY, Justify::RIGHT,
            tagStrong ? Fonts::getStrong() : Fonts::getNormal(), tagColor, ctx.fontSize);
        ctx.nextLine();
        const int numbersChars = numbers[0] ? static_cast<int>(std::strlen(numbers)) + 1 : 0;
        const int room = static_cast<int>((rowRight - textX) / cw) - numbersChars;
        char cut[64];
        if (static_cast<int>(std::strlen(task)) > room && room > 3) {
            snprintf(cut, sizeof(cut), "%.*s...", room - 3, task);
            task = cut;
        }
        ctx.parent->addString(task, textX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
        if (numbers[0]) {
            ctx.parent->addString(numbers, rowRight, ctx.currentY, Justify::RIGHT,
                Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
        }
        ctx.nextLine();
    };

    // THE LIST'S ROWS, gathered before anything is drawn. Short vectors per
    // rebuild: the panel rebuilds on a click, the 1 Hz tick or a scroll, never
    // per frame at rest.
    // ONE RULE: a hidden row is not a row until it is EARNED. That covers both
    // unlisted groups (Achievements::isUnlistedGroup), and nothing else is
    // filtered - the Completion rows are a dashboard and are always listed, at
    // 0% if that is where you are.
    std::vector<AchievementManager::Row> rows;
    rows.reserve(static_cast<size_t>(ach.rowCount()));
    for (int i = 0; i < ach.rowCount(); ++i) {
        AchievementManager::Row r = ach.row(i);
        if (r.entry->hidden && r.tier == 0) continue;
        rows.push_back(r);
    }
    // A toast's click: the list comes to rest on that row (scrollToHere, below),
    // then the request is forgotten. A row not listed (hidden, unearned) leaves
    // the list where it was. By id, not address: kCatalogue is a header
    // constexpr, so each translation unit holds its own copy and the manager's
    // pointers are not this file's.
    const int jumpTo = ctx.parent->m_achievementsJumpTo;
    ctx.parent->m_achievementsJumpTo = -1;
    const char* jumpId = (jumpTo >= 0 && jumpTo < Achievements::COUNT) ? Achievements::kCatalogue[jumpTo].id : nullptr;

    // === PROGRESS: achievements earned, which is what a player counts. NOT
    // tiers - the tier total is the hidden Completionist row's business, and
    // two competing "how far through" percentages on one page is why the
    // listed one was cut. ===
    const int earned = ach.earnedAchievements();
    const int total = ach.listedAchievements();
    const int pct = total > 0 ? (earned * 100) / total : 0;
    // The prestige level rides on the HEADING rather than taking an entry of
    // its own. An entry is two rows, and this tab sets the panel's height for
    // every other tab (settings_fit_test) -- two rows here cost two rows of
    // screen everywhere, and with the trade's button below them that was enough
    // to push the whole panel off the bottom in developer mode. A heading is
    // free, and it sits directly above the completion figure the level was
    // traded for, which is where it reads anyway.
    const int prestige = StatsManager::getInstance().getPrestige();
    if (prestige > 0) {
        char heading[32];
        snprintf(heading, sizeof(heading), TR("Progress (Prestige %d)"), prestige);
        ctx.addSectionHeading(heading);
    } else {
        ctx.addSectionHeading(TR("Progress"));
    }
    {
        // "47%" or "47% (+3)". The percentage is the LISTED set alone, so it
        // cannot pass 100% and cannot read as a counting fault; the bonus is
        // every row earned outside that set - secrets and misfortunes both -
        // and says what the overshoot used to say, in a number that admits
        // what it is. Nothing at all when there is none to report.
        char pctText[16];
        const int bonus = ach.bonusAchievements();
        if (bonus > 0) snprintf(pctText, sizeof(pctText), "%d%% (+%d)", pct, bonus);
        else           snprintf(pctText, sizeof(pctText), "%d%%", pct);
        snprintf(buf, sizeof(buf), "%d / %d", earned, total);
        addEntry(useIcons ? assets.getIconSpriteIndex("award") : 0, badgeSprite[1], colors.getAccent(),
                 TR("Unlocked"), pctText, colors.getSecondary(), /*tagStrong=*/false,
                 total > 0 ? std::min(1.0f, static_cast<float>(earned) / static_cast<float>(total)) : 0.0f,
                 colors.getAccent(), TR("Achievements earned, at any tier"), buf);
    }



    // One catalogue row as an entry.
    auto addRow = [&](const AchievementManager::Row& r) {
        const Achievements::Entry& e = *r.entry;
        const bool earnedRow = r.tier > 0;
        // A SWEEP row is coloured by the metal it MEASURES, not by what it has
        // earned - all four carry the same glyph, and the bronze/silver/gold/
        // platinum tint is the only thing telling them apart. Colouring them by
        // their own tier would paint all four the same muted grey until one hit
        // 100%, which is every day for everybody.
        const int sweepMetal = Achievements::completionMetal(e);
        // A one-shot has no metal: earned in the accent, like the summary.
        const unsigned long metal = sweepMetal > 0
            ? PodiumColors::tierColor(sweepMetal, colors.getMuted())
            : (Achievements::isOneShot(e)
                ? (earnedRow ? colors.getAccent() : colors.getMuted())
                : PodiumColors::tierColor(r.tier, colors.getMuted()));

        Achievements::formatProgress(e, r.value, buf, sizeof(buf));
        // Who is carrying a per-track / per-bike maximum row, folded into the
        // title; "" for the rest, which then reads as the bare title.
        char leader[64];
        ach.leaderFor(e, leader, sizeof(leader));
        char rowTitle[96];
        if (leader[0]) snprintf(rowTitle, sizeof(rowTitle), "%s (%s)", I18n::trCached(e.title), leader);
        else           snprintf(rowTitle, sizeof(rowTitle), "%s", I18n::trCached(e.title));
        // The task: the next tier's sentence, or the top tier's once complete.
        char task[64];
        Achievements::formatDescription(e, std::min(r.tier + 1, e.tierCount), task, sizeof(task));
        // A complete row's band takes its metal; every other band is the accent,
        // so the column reads as one instrument with the finished ones lit.
        // Index 0 is NO badge, and that is what an unearned row gets -- a Sweep
        // row included. The metal art is what says a tier has been taken; drawn
        // before it is, it says the opposite of the "Locked" beside it. The four
        // Sweeps still read apart while locked because their GLYPH takes the
        // metal they measure (tileColor below), which is the thing that had to
        // be true: colouring them by their own tier would paint all four the
        // same muted grey until one hit 100%.
        const int badgeTier = !earnedRow ? 0
            : (sweepMetal > 0 ? sweepMetal
                              : (Achievements::isOneShot(e) ? Achievements::TIER_COUNT : r.tier));
        // THE TAG IS MUTED UNTIL SOMETHING IS EARNED, Sweep rows included. Their
        // metal is forced (it is the metal they MEASURE, not one they hold), and
        // that was reaching the tag too -- so "Locked" was written in bronze on
        // the Bronze Sweep and in gold on the Gold Sweep while every other locked
        // row's read muted. The metal still reaches the GLYPH, which is what
        // keeps the four apart.
        addEntry(useIcons && e.icon ? assets.getIconSpriteIndex(e.icon) : 0, badgeSprite[badgeTier],
                 (sweepMetal > 0 || earnedRow) ? metal : colors.getMuted(),
                 rowTitle, I18n::trCached(Achievements::tierLabel(e, r.tier)),
                 earnedRow ? metal : colors.getMuted(), earnedRow,
                 Achievements::progressFraction(e, r.value),
                 r.tier >= e.tierCount ? metal : colors.getAccent(), task, buf);
    };

    // === THE LIST: every group in a card of its own, scrolling as one ===
    // As tall as a page of the old pager was with its heading and buttons
    // (the heading, ENTRIES_PER_PAGE entries, the junction and the pager row),
    // so the panel this tab sets the height of keeps its size. The groups' own
    // names head the cards, so the list needs no heading of its own.
    ctx.beginUntitledSection();
    const float listTop = ctx.currentY;
    ctx.beginScroll(ScrollView::ACHIEVEMENTS,
                    static_cast<float>(ENTRIES_PER_PAGE * 2 + 2) + ctx.spacingHeight() / ctx.lineHeightNormal);
    // The group at the list's top, for the tests: the last heading at or above it.
    const float topLines = ctx.parent->m_scrolls[ScrollView::ACHIEVEMENTS].target;
    ctx.parent->m_achievementsTopGroup = "";
    for (int g = 0; g < static_cast<int>(Achievements::Group::COUNT); ++g) {
        const Achievements::Group group = static_cast<Achievements::Group>(g);
        bool any = false;
        for (const AchievementManager::Row& r : rows) any = any || r.entry->group == group;
        if (!any) continue;
        // SPELLED OUT where the group's rows are outside the Unlocked figure and
        // the Sweeps (Achievements::countsTowardCompletion): it answers "why did
        // that not move the bar", which is asked the moment a row lands.
        const char* outside = Achievements::countsTowardCompletion(group)
                                  ? "" : TR(" (does not count towards progress)");
        ctx.scrollGroup();
        if ((ctx.currentY - listTop) / ctx.lineHeightNormal <= topLines + ScrollView::EPS) {
            ctx.parent->m_achievementsTopGroup = Achievements::groupName(group);
        }
        bool first = true;   // the group's name shares its first entry's row
        for (const AchievementManager::Row& r : rows) {
            if (r.entry->group != group) continue;
            if (jumpId && std::strcmp(r.entry->id, jumpId) == 0) ctx.scrollToHere();
            const bool drawn = ctx.scrollRow(first ? 3.0f : 2.0f);
            if (drawn && first) {
                snprintf(buf, sizeof(buf), "%s%s", Achievements::groupName(group), outside);
                ctx.addListHeading(buf);
            }
            first = false;
            if (!drawn) continue;
            addRow(r);
        }

        // THE TRADE: under the four Sweep rows, at the foot of the Completion
        // group, and nowhere else. What it trades away is exactly what those four
        // rows measure, so it belongs at the foot of them, where a player who has
        // just read "Every listed achievement at Platinum" finds it. A row of its
        // own in the list (height the theme's), so it scrolls with them.
        //
        // ONE ROW, armed or not: the warning is the button's TOOLTIP, and the
        // protection is the two-click arm -- "Confirm?" on a Negative button says
        // what the second click does. Moving the list disarms it (updateScrolls).
        if (group == Achievements::Group::Completion && ach.isPrestigeAvailable() && ctx.scrollRow()) {
            const bool armed = ctx.parent->m_prestigeConfirmed;
            // The junction every other in-tab button opens with.
            ctx.addSpacing();
            ctx.addActionButton(armed ? TR("Confirm?") : TR("Prestige"), 10,
                                SettingsHud::ClickRegion::ACHIEVEMENTS_PRESTIGE,
                                SettingsLayoutContext::ButtonRole::Negative, true,
                                "achievements.prestige");
        }
    }
    ctx.endScroll();

    // No active HUD for a global manager tab
    return nullptr;
}
