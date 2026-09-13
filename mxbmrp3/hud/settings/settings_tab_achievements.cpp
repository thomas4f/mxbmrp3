// ============================================================================
// hud/settings/settings_tab_achievements.cpp
// Tab renderer for Achievements: the lifetime numbers the plugin keeps, shown
// as tiered progress toward the next Bronze/Silver/Gold/Platinum threshold --
// one entry is both the number and the achievement it feeds. Plus the toast
// card's controls (global, like the Director's status button). The on-track
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
// A PAGE IS A GROUP (Achievements::Group, the section heading), or an
// ENTRIES_PER_PAGE chunk of one ("Plugin 2/4"): related rows read together. A group with no row
// on this game (a kart has no Freestyle) is no page, and a hidden row is not a
// row until it is earned (Achievements::Entry::hidden). The
// pager is the Riders tab's "< Page x/y >". The row says what the next tier
// asks; it has no hover of its own (no tooltip, no tint) -- nothing to find
// by moving the mouse.
//
// ORDER within a page: the catalogue's, always. Rows used to re-sort on every
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

using Achievements::ENTRIES_PER_PAGE;   // the catalogue's own page size

// The metal each tier is drawn in; locked rows take the muted text colour.
unsigned long tierColor(int tier, const ColorConfig& colors) {
    switch (tier) {
        case 1: return PodiumColors::BRONZE;
        case 2: return PodiumColors::SILVER;
        case 3: return PodiumColors::GOLD;
        case 4: return PluginUtils::makeColor(229, 228, 226);
        default: return colors.getMuted();
    }
}

}  // namespace

bool SettingsHud::handleClickTabAchievements(const ClickRegion& region) {
    // ACHIEVEMENTS_TOASTS_TOGGLE is deliberately NOT here: the tab list carries
    // that same toggle as its row checkbox, and a tab-scoped handler only runs
    // while its own tab is open -- so it lives in dispatchRegion's common switch
    // (settings_hud_input.cpp), like the spotter's.
    switch (region.type) {
        // DISARMED ON THE WAY OUT, both of them: the Prestige button is drawn on
        // the Completion page only, so leaving that page has to cancel a half-made
        // decision or "Confirm?" waits on a page nobody is on. Here rather than in
        // the render pass, which is where it was: a rebuild is a drawing of state,
        // and a drawing that edits what it draws is the one thing the panel avoids.
        // Leaving the TAB is already covered by disarmResets().
        case ClickRegion::ACHIEVEMENTS_PAGE_PREV:
            if (m_achievementsPage > 0) {
                m_achievementsPage--;
                m_prestigeConfirmed = false;
                rebuildRenderData();
            }
            return true;
        case ClickRegion::ACHIEVEMENTS_PAGE_NEXT:
            m_achievementsPage++;   // clamped against the page count at render
            m_prestigeConfirmed = false;
            rebuildRenderData();
            return true;
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
    // Cell geometry: "< value >" with a 7-character value, in the control column.
    constexpr int CELL_VALUE_CHARS = 7;
    const float colX = ctx.labelX + PluginUtils::calculateMonospaceTextWidth(11, ctx.fontSize);

    ctx.addSectionHeading("Toasts");

    // One row: its label and a row-wide tooltip region.
    auto beginRow = [&](const char* label, const char* tooltipId) {
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, ctx.rowSpanWidth(), ctx.lineHeightNormal, tooltipId));
        ctx.parent->addString(label, ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
    };
    // "< value >" with two arrow regions of the given types, like addWidgetRow's
    // inline cycle. Returns the index of the first region pushed (the down arrow)
    // so a data-driven caller can stamp its descriptor index onto both.
    auto addCell = [&](const char* value,
                       SettingsHud::ClickRegion::Type downType,
                       SettingsHud::ClickRegion::Type upType,
                       BaseHud* target, bool enabled, bool isOff) -> size_t {
        const size_t first = ctx.parent->m_clickRegions.size();
        float cx = colX;
        const unsigned long valueColor = (enabled && !isOff) ? colors.getPrimary() : colors.getMuted();
        if (enabled) {
            ctx.parent->addString("<", cx, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                cx, ctx.currentY, cw * 2, ctx.lineHeightNormal, downType, target, 0, false, 0));
        }
        cx += cw * 2;
        const std::string formatted = SettingsLayoutContext::formatValue(value, CELL_VALUE_CHARS, false);
        ctx.parent->addString(formatted.c_str(), cx, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, ctx.fontSize);
        cx += PluginUtils::calculateMonospaceTextWidth(CELL_VALUE_CHARS, ctx.fontSize);
        if (enabled) {
            ctx.parent->addString(" >", cx, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                cx, ctx.currentY, cw * 2, ctx.lineHeightNormal, upType, target, 0, false, 0));
        }
        return first;
    };
    auto addToggleCell = [&](bool on, SettingsHud::ClickRegion::Type type, BaseHud* target, bool enabled) {
        addCell(on ? "On" : "Off", type, type, target, enabled, !on);
    };
    // Every row but Visible greys out with the toasts off: live arrows under an
    // Off switch read as a control that does nothing.

    // Visible: the toast master (common handler, shared with the tab-list checkbox).
    beginRow("Visible", "achievements.toasts");
    addToggleCell(toastsOn, SettingsHud::ClickRegion::ACHIEVEMENTS_TOASTS_TOGGLE, nullptr, true);
    ctx.nextLine();

    beginRow("Title", "common.title");
    if (widget) addToggleCell(widget->getShowTitle(), SettingsHud::ClickRegion::TITLE_TOGGLE, widget, toastsOn);
    ctx.nextLine();

    // Texture: the same rule addWidgetRow applies -- a HUD with texture variants
    // cycles them, everything else cycles its panel theme.
    beginRow("Texture", "common.texture");
    if (widget) {
        const bool hasTextures = !widget->getAvailableTextureVariants().empty();
        if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
            const std::string& ov = widget->getThemeOverride();
            std::string themeValue = "Default";
            if (ov == BaseHud::THEME_NONE) {
                themeValue = "None";
            } else if (!ov.empty()) {
                if (const ThemeAsset* t = AssetManager::getInstance().getThemeByName(ov)) {
                    themeValue = t->displayName;
                }
            }
            addCell(themeValue.c_str(), SettingsHud::ClickRegion::HUD_THEME_DOWN,
                    SettingsHud::ClickRegion::HUD_THEME_UP, widget, toastsOn, false);
        } else {
            char texValue[8];
            const int variant = widget->getTextureVariant();
            snprintf(texValue, sizeof(texValue), (!hasTextures || variant == 0) ? "Off" : "%d", variant);
            addCell(texValue, SettingsHud::ClickRegion::TEXTURE_VARIANT_DOWN,
                    SettingsHud::ClickRegion::TEXTURE_VARIANT_UP, widget, toastsOn && hasTextures,
                    !hasTextures || variant == 0);
        }
    }
    ctx.nextLine();

    beginRow("Opacity", "common.opacity");
    if (widget) {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(widget->getBackgroundOpacity() * 100.0f + 0.5f));
        addCell(buf, SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN,
                SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP, widget, toastsOn, false);
    }
    ctx.nextLine();

    beginRow("Scale", "common.scale");
    if (widget) {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(widget->getScale() * 100.0f + 0.5f));
        addCell(buf, SettingsHud::ClickRegion::SCALE_DOWN,
                SettingsHud::ClickRegion::SCALE_UP, widget, toastsOn, false);
    }
    ctx.nextLine();

    // Duration: a data-driven stepped control in a cell: register the
    // descriptor as addSteppedControl does, then stamp its index on the two
    // arrow regions the cell pushed.
    beginRow("Duration", "achievements.toast_duration");
    {
        auto sc = SettingsHud::SteppedControl::clampInt(
            ach.toastDurationMsPtr(), AchievementManager::TOAST_DURATION_STEP_MS,
            AchievementManager::MIN_TOAST_DURATION_MS, AchievementManager::MAX_TOAST_DURATION_MS,
            widget);
        const int steppedIndex = static_cast<int>(ctx.parent->m_steppedControls.size());
        ctx.parent->m_steppedControls.push_back(sc);
        snprintf(buf, sizeof(buf), "%ds", ach.getToastDurationMs() / 1000);
        const size_t first = addCell(buf, SettingsHud::ClickRegion::STEPPED_DOWN,
                                     SettingsHud::ClickRegion::STEPPED_UP, nullptr, toastsOn, false);
        for (size_t r = first; r < ctx.parent->m_clickRegions.size(); ++r) {
            ctx.parent->m_clickRegions[r].steppedIndex = steppedIndex;
        }
    }
    ctx.nextLine();

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

    // WHICH PAGE, worked out BEFORE anything is drawn. It used to sit below the
    // Progress card, which was fine while the card was the same on every page --
    // but the Prestige button inside it is a Completion-page control, and a
    // control cannot ask which page it is on after the page has been drawn.
    // Nothing in here emits a quad or a string; it is vectors and an index.
    // The rows this game can move, and the groups they fall in: a page per
    // group that has one. Short vectors per rebuild: the panel rebuilds on a
    // click or the 1 Hz tick, never per frame.
    // ONE RULE: a hidden row is not a row until it is EARNED. That covers both
    // unlisted groups (Achievements::isUnlistedGroup), and nothing else is
    // filtered - the Completion rows are a dashboard and are always listed, at
    // 0% if that is where you are. A second rule used to hide those until they
    // left zero; it is gone, and this comment described it for two commits
    // after it went.
    std::vector<AchievementManager::Row> rows;
    rows.reserve(static_cast<size_t>(ach.rowCount()));
    int groupRows[static_cast<int>(Achievements::Group::COUNT)] = {};
    for (int i = 0; i < ach.rowCount(); ++i) {
        AchievementManager::Row r = ach.row(i);
        if (r.entry->hidden && r.tier == 0) continue;
        rows.push_back(r);
        groupRows[static_cast<int>(r.entry->group)]++;
    }
    // Pages: each group in chunks of ENTRIES_PER_PAGE, in group order.
    struct Page { Achievements::Group group; int first; int chunk; int chunks; };
    std::vector<Page> pages;
    for (int g = 0; g < static_cast<int>(Achievements::Group::COUNT); ++g) {
        const int chunks = (groupRows[g] + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;
        for (int c = 0; c < chunks; ++c) {
            pages.push_back({ static_cast<Achievements::Group>(g), c * ENTRIES_PER_PAGE, c + 1, chunks });
        }
    }
    const int pageCount = std::max(1, static_cast<int>(pages.size()));
    int& page = ctx.parent->m_achievementsPage;
    // A toast's click: the page holding its row, found the way a page is
    // drawn -- the row's place in its group, in chunks -- and then forgotten.
    // A row not listed (hidden, unearned) leaves the page.
    if (ctx.parent->m_achievementsJumpTo >= 0 && ctx.parent->m_achievementsJumpTo < Achievements::COUNT) {
        const Achievements::Entry& target = Achievements::kCatalogue[ctx.parent->m_achievementsJumpTo];
        std::vector<AchievementManager::Row> inGroup;
        for (const AchievementManager::Row& r : rows) {
            if (r.entry->group == target.group) inGroup.push_back(r);
        }
        // By id, not address: kCatalogue is a header constexpr, so each
        // translation unit holds its own copy and the manager's pointers are
        // not this file's.
        for (size_t i = 0; i < inGroup.size(); ++i) {
            if (std::strcmp(inGroup[i].entry->id, target.id) != 0) continue;
            const int first = static_cast<int>(i / ENTRIES_PER_PAGE) * ENTRIES_PER_PAGE;
            for (size_t p = 0; p < pages.size(); ++p) {
                if (pages[p].group == target.group && pages[p].first == first) { page = static_cast<int>(p); break; }
            }
            break;
        }
    }
    ctx.parent->m_achievementsJumpTo = -1;
    if (page >= pageCount) page = pageCount - 1;
    if (page < 0) page = 0;
    const Page current = pages.empty() ? Page{ Achievements::Group::Mileage, 0, 1, 1 }
                                       : pages[static_cast<size_t>(page)];
    ctx.parent->m_achievementsPageGroup = Achievements::groupName(current.group);
    std::vector<AchievementManager::Row> groupSorted;
    groupSorted.reserve(rows.size());
    for (const AchievementManager::Row& r : rows) {
        if (r.entry->group == current.group) groupSorted.push_back(r);
    }
    const size_t firstRow = static_cast<size_t>(current.first);
    const size_t lastRow = std::min(groupSorted.size(), firstRow + ENTRIES_PER_PAGE);

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
        snprintf(heading, sizeof(heading), "Progress (Prestige %d)", prestige);
        ctx.addSectionHeading(heading);
    } else {
        ctx.addSectionHeading("Progress");
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
                 "Unlocked", pctText, colors.getSecondary(), /*tagStrong=*/false,
                 total > 0 ? std::min(1.0f, static_cast<float>(earned) / static_cast<float>(total)) : 0.0f,
                 colors.getAccent(), "Achievements earned, at any tier", buf);
    }



    // === THIS PAGE'S GROUP ===
    // SPELLED OUT where the page's rows are outside the Unlocked figure and the
    // Sweeps (Achievements::countsTowardCompletion). "(not counted)" read as
    // something being wrong with the page; this names the thing they do not count
    // towards, and "progress" is the card two sections up, so the reader has just
    // seen it. Said on the page rather than in a footnote: it answers "why did
    // that not move the bar", which is asked the moment a row lands.
    const char* outside = Achievements::countsTowardCompletion(current.group)
                              ? "" : " (does not count towards progress)";
    if (current.chunks > 1) {
        snprintf(buf, sizeof(buf), "%s %d/%d%s", Achievements::groupName(current.group),
                 current.chunk, current.chunks, outside);
    } else {
        snprintf(buf, sizeof(buf), "%s%s", Achievements::groupName(current.group), outside);
    }
    ctx.addSectionHeading(buf);
    // Where the page's BODY starts. Everything below is padded back out to one
    // fixed height from here, so the panel is the same height on every page --
    // see the pad at the foot.
    const float pageBodyTop = ctx.currentY;
    for (size_t i = firstRow; i < lastRow; ++i) {
        const AchievementManager::Row& r = groupSorted[i];
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
            ? tierColor(sweepMetal, colors)
            : (Achievements::isOneShot(e)
                ? (earnedRow ? colors.getAccent() : colors.getMuted())
                : tierColor(r.tier, colors));

        Achievements::formatProgress(e, r.value, buf, sizeof(buf));
        // Who is carrying a per-track / per-bike maximum row, folded into the
        // title; "" for the rest, which then reads as the bare title.
        char leader[64];
        ach.leaderFor(e, leader, sizeof(leader));
        char rowTitle[96];
        if (leader[0]) snprintf(rowTitle, sizeof(rowTitle), "%s (%s)", e.title, leader);
        else           snprintf(rowTitle, sizeof(rowTitle), "%s", e.title);
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
                 rowTitle, Achievements::tierLabel(e, r.tier),
                 earnedRow ? metal : colors.getMuted(), earnedRow,
                 Achievements::progressFraction(e, r.value),
                 r.tier >= e.tierCount ? metal : colors.getAccent(), task, buf);
    }

    // THE TRADE: under the four Sweep rows, on the Completion page, and nowhere
    // else. It used to sit in the Progress card, which is the tab's header above
    // every one of its pages -- so a button that empties the whole ladder
    // followed the player around the tab. What it trades away is exactly what
    // those four rows measure, so it belongs at the foot of them, where a player
    // who has just read "Every listed achievement at Platinum" finds it.
    //
    // No heading of its own: a section heading opens a new card, and a card
    // costs its own padding plus the junction gap above it -- which on the
    // tallest tab in the panel pushed the whole PANEL past the screen in
    // developer mode (settings_render_test measures exactly that, and the panel
    // does not scroll, so the overflow is unreachable UI). On this card it is
    // one row inside the space the short page was padding out anyway.
    //
    // Paging away cancels a half-made decision; that disarm lives with the pager
    // CLICK (handleClickTabAchievements), not here -- a render pass draws state,
    // it does not edit it.
    const bool prestigeHere = ach.isPrestigeAvailable() &&
                              current.group == Achievements::Group::Completion;
    if (prestigeHere) {
        // ONE ROW, and the same one row armed or not. This tab is the tallest in
        // the panel, so its height IS the panel's height on every other tab
        // (settings_fit_test), and the panel does not scroll -- a second row
        // here put Save and Close off the bottom of the screen in developer
        // mode, which settings_render_test measures directly. An inline note
        // would also have made the ARMED state a row taller than the unarmed
        // one, so the state that fit would have been the state nobody is in
        // when they click.
        //
        // So the warning is a row TOOLTIP rather than a note: the same hover
        // every other row on this tab carries, at no height. The protection was
        // never the sentence anyway -- it is the two-click arm, and "Confirm?"
        // on a Negative button says what the second click does.
        const bool armed = ctx.parent->m_prestigeConfirmed;
        // The junction every other in-tab button opens with -- the [panel] gap
        // that belongs to the STACK, where the button's own margins belong to the
        // box (see buttonRow). Without it this button sat tight against the last
        // row while Copy, Check Now and Run Sweep all stood off theirs.
        ctx.addSpacing();
        // THE WARNING RIDES ON THE BUTTON'S OWN REGION. It used to be a row-wide
        // TOOLTIP_ROW pushed just before, and hover resolves to the FIRST region
        // under the pointer: the row shadowed the button, so the tooltip showed
        // but the button never lit up, and the hover box was the whole row
        // instead of the button. One rect now answers all three.
        ctx.addActionButton(armed ? "Confirm?" : "Prestige", 10,
                            SettingsHud::ClickRegion::ACHIEVEMENTS_PRESTIGE,
                            SettingsLayoutContext::ButtonRole::Negative, true,
                            "achievements.prestige");
    }

    // The pager, at the foot of a FULL page whatever this page holds: a short
    // page (a group's last chunk) would otherwise lift it, and a button that
    // moves between pages has to be chased. Only when there is more than one.
    //
    // PADDED TO A FIXED BODY HEIGHT, not by the rows it is short of. Counting
    // rows assumed rows were the only thing on a page, so the Completion page --
    // four rows plus the Prestige button -- came out taller than the rest and
    // the whole panel changed height when you paged onto it. Padding to a target
    // measures whatever the page actually emitted, so anything that lands here
    // later is absorbed too, and it does so in the theme's own units: the
    // button's height is its themed margins and insets, and the difference is
    // taken from the same numbers rather than from an assumed row count.
    if (pageCount > 1) {
        const float fullBody = static_cast<float>(ENTRIES_PER_PAGE) * ctx.lineHeightNormal * 2.0f;
        const float used = ctx.currentY - pageBodyTop;
        if (used < fullBody) ctx.currentY += fullBody - used;
        ctx.addSpacing();   // the junction above a button row, like every in-tab button
    }
    ctx.addPager(page, pageCount, SettingsHud::ClickRegion::ACHIEVEMENTS_PAGE_PREV,
                 SettingsHud::ClickRegion::ACHIEVEMENTS_PAGE_NEXT);

    // No active HUD for a global manager tab
    return nullptr;
}
